#ifdef PLATFORM_RP2040
#include "pico/stdlib.h"
#include "tusb.h"
#include "manager/PrinterManager.h"
#include "protocol/TsplHandler.h"
#include "protocol/PclHandler.h"
#include "protocol/EscpHandler.h"
#include "protocol/PrintJob.h"
#include "protocol/ProtocolType.h"
#include "generator/ITextGenerator.h"
#include "generator/TextConfig.h"
#include "sio/SioPrinterEmulator.h"
#include "Rp2040UsbTransport.h"
#include "SioUart.h"
#include "FlashConfig.h"
#include "util/Logger.h"
#include <memory>

static const char* TAG = "main_rp2040";

// ── Globals shared between TinyUSB callbacks and main loop ────────────────────

static PrinterManager* g_manager = nullptr;

// Deferred-action flags (set in callbacks, consumed in main loop)
static volatile uint8_t s_pendingMountAddr = 0; // nonzero = printer mount needs processing

// ── TinyUSB host callbacks ─────────────────────────────────────────────────────

extern "C" {

void tuh_mount_cb(uint8_t devAddr) {
    LOG_INFO(TAG, "USB device mounted: addr=%u", devAddr);
    s_pendingMountAddr = devAddr;
}

void tuh_umount_cb(uint8_t devAddr) {
    LOG_INFO(TAG, "USB device unmounted: addr=%u", devAddr);
    Rp2040UsbTransport::onUnmount(devAddr);

    if (g_manager) {
        g_manager->onDetach(0, devAddr);
    }
}

} // extern "C"

// ── Mount processing (called from main loop, safe to call tuh_descriptor_*) ──

static void processPendingMount(uint8_t devAddr, PrinterManager& manager) {
    static uint8_t cfgBuf[512];
    if (tuh_descriptor_get_configuration_sync(devAddr, 0, cfgBuf, sizeof(cfgBuf)) != XFER_RESULT_SUCCESS) {
        LOG_WARN(TAG, "addr=%u: failed to get config descriptor", devAddr);
        return;
    }

    auto* cfg = reinterpret_cast<tusb_desc_configuration_t const*>(cfgBuf);
    uint8_t const* p   = cfgBuf + sizeof(tusb_desc_configuration_t);
    uint8_t const* end = cfgBuf + tu_le16toh(cfg->wTotalLength);

    int8_t  ifaceNum  = -1;
    uint8_t epOut     = 0, epIn = 0;
    bool    hasEpIn   = false;
    tusb_desc_endpoint_t const* epOutDesc = nullptr;
    tusb_desc_endpoint_t const* epInDesc  = nullptr;

    while (p < end) {
        if (tu_desc_len(p) == 0) break;

        if (tu_desc_type(p) == TUSB_DESC_INTERFACE) {
            auto* iface = reinterpret_cast<tusb_desc_interface_t const*>(p);
            if (iface->bInterfaceClass == TUSB_CLASS_PRINTER) {
                ifaceNum = (int8_t)iface->bInterfaceNumber;
                uint8_t const* q = p + iface->bLength;
                for (int e = 0; e < iface->bNumEndpoints && q < end; ) {
                    if (tu_desc_len(q) == 0) break;
                    if (tu_desc_type(q) == TUSB_DESC_ENDPOINT) {
                        auto* ep = reinterpret_cast<tusb_desc_endpoint_t const*>(q);
                        if (ep->bmAttributes.xfer == TUSB_XFER_BULK) {
                            if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_OUT && epOut == 0) {
                                epOut = ep->bEndpointAddress; epOutDesc = ep;
                            } else if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN && !hasEpIn) {
                                epIn = ep->bEndpointAddress; hasEpIn = true; epInDesc = ep;
                            }
                        }
                        ++e;
                    }
                    q = tu_desc_next(q);
                }
                break; // first printer interface wins
            }
        }
        p = tu_desc_next(p);
    }

    if (ifaceNum < 0 || epOut == 0 || epOutDesc == nullptr) {
        LOG_INFO(TAG, "addr=%u: no printer-class interface with BULK OUT", devAddr);
        return;
    }

    if (!tuh_edpt_open(devAddr, epOutDesc)) {
        LOG_ERROR(TAG, "addr=%u: tuh_edpt_open(OUT 0x%02x) failed", devAddr, epOut);
        return;
    }
    if (hasEpIn && epInDesc != nullptr) {
        if (!tuh_edpt_open(devAddr, epInDesc)) {
            LOG_WARN(TAG, "addr=%u: tuh_edpt_open(IN) failed, treating as write-only", devAddr);
            hasEpIn = false; epIn = 0;
        }
    }

    Rp2040UsbTransport::onMount(devAddr, (uint8_t)ifaceNum, epOut, epIn, hasEpIn);

    UsbDeviceDescriptor udesc;
    udesc.deviceAddress = devAddr;
    manager.onAttach(udesc);
    manager.openDevice(udesc);   // bind transport to this device

    LOG_INFO(TAG, "addr=%u: printer ready (epOut=0x%02x)", devAddr, epOut);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    stdio_init_all();

    // Initialise TinyUSB host stack on rhport 0 (RP2040's USB controller)
    const tusb_rhport_init_t host_init = {TUSB_ROLE_HOST, TUSB_SPEED_AUTO};
    tusb_init(0, &host_init);

    Logger::setLevel(LogLevel::INFO);
    LOG_INFO(TAG, "USB Printer Driver RP2040 starting");

    // Printer manager owns the transport (static lifetime on RP2040).
    // Keep a raw pointer for direct async writes in the ESC/POS path —
    // the manager takes ownership via unique_ptr but the object never moves.
    auto transportPtr = std::make_unique<Rp2040UsbTransport>();
    Rp2040UsbTransport* rawTransport = transportPtr.get();
    static PrinterManager manager(std::move(transportPtr));
    g_manager = &manager;

    // Register handlers in specificity order: TSPL → ESCP → PCL
    manager.registerHandler(std::make_unique<TsplHandler>());
    manager.registerHandler(std::make_unique<EscpHandler>());
    manager.registerHandler(std::make_unique<PclHandler>());

    // Text generator for the selected personality
    TextConfig cfg;
#if ATARI_PRINTER_PERSONALITY == TSPL
    // Allow up to ~196 lines per label (600mm roll) before auto page-break.
    // Batch mode accumulates all LPRINT/LLIST lines into one label.
    cfg.labelHeightDots = 4800;
#endif

    // Auto-load NVM slot 0 on boot (persists user configuration across power cycles).
    // Falls back to compiled-in defaults if no valid config is stored.
    {
        TextConfig nvmCfg;
        if (FlashConfig::load(0, nvmCfg)) {
            cfg = nvmCfg;
            LOG_INFO(TAG, "Boot: NVM slot 0 loaded");
        } else {
            LOG_INFO(TAG, "Boot: no NVM config in slot 0, using compiled defaults");
        }
    }

    auto generator = makeTextGenerator(
#if   ATARI_PRINTER_PERSONALITY == 1025
        ProtocolType::PCL
#elif ATARI_PRINTER_PERSONALITY == TSPL
        ProtocolType::TSPL
#elif ATARI_PRINTER_PERSONALITY == 1028
        ProtocolType::ESCPOS  // 80mm thermal receipt printer (line-oriented, like Atari 820)
#else
        ProtocolType::ESCP   // 825 and 1027
#endif
    );
    generator->configure(cfg);

    // SIO emulator
    LineAssembler::Mode mode =
#if ATARI_PRINTER_PERSONALITY == 1025
        LineAssembler::Mode::COL_80;
#else
        LineAssembler::Mode::COL_40;
#endif

    SioUart::Config sioCfg;
    static SioUart sioUart(sioCfg);
    sioUart.init();

    // GPIO 8: print-job termination button (active-low, internal pull-up).
    // Press to flush the accumulated label and send it to the printer.
    gpio_init(8);
    gpio_set_dir(8, GPIO_IN);
    gpio_pull_up(8);

    static SioPrinterEmulator sioEmulator(sioUart, *generator, mode, cfg);

    LOG_INFO(TAG, "Entering main loop");

    static uint32_t s_lastButtonMs  = 0;
    static bool     s_buttonWasDown = false;

    // ESC/POS batch flush threshold. Lines accumulate after each SIO tick;
    // they are sent as one USB transfer when no new data arrives for this long.
    // 1000ms is long enough that a pause between separate LPRINT statements
    // doesn't split a batch prematurely, yet is still fast from the user's view.
    // NOTE: intentionally NOT based on lastActivityMs() — that timer resets on
    // STATUS polls too, so it never goes quiet while P: is open.
    static constexpr uint32_t ESCPOS_FLUSH_MS = 1000;

    // s_accumBuf: lines accumulate here as they arrive from the SIO tick.
    // s_escposDataMs: time of the last append (0 = nothing pending).
    static std::vector<uint8_t> s_accumBuf;
    static uint32_t             s_escposDataMs  = 0;

    // Async write state — pending batch drained one RP2040_MAX_PACKET chunk per iteration.
    static std::vector<uint8_t> s_escposJob;
    static size_t               s_escposOffset = 0;

    while (true) {
        tuh_task();                    // TinyUSB host tick

        if (s_pendingMountAddr) {
            uint8_t addr = s_pendingMountAddr;
            s_pendingMountAddr = 0;
            processPendingMount(addr, manager);
        }

        // GPIO 8 (active-low):
        // ESC/POS: ignored while accumulating or sending. Once the batch has
        //   been fully sent (both buffers empty, data timer cleared), a press
        //   queues a partial cut (GS V 66 0). Debounce: 5-second re-arm.
        // Other personalities: flush label via manager; 5s debounce + 2s SIO silence.
        uint32_t nowMs   = to_ms_since_boot(get_absolute_time());
        bool     btnDown = !gpio_get(8);
        // Detect press edge and suppress contact bounce: require 200ms quiet since
        // the last accepted edge before recognising a new one.
        bool     btnEdge = btnDown && !s_buttonWasDown
                           && (nowMs - s_lastButtonMs) > 200;
        s_buttonWasDown  = btnDown;

        if (btnEdge) {
            s_lastButtonMs = nowMs; // start debounce window for both paths
            if (generator->protocol() == ProtocolType::ESCPOS) {
                // Idle state already proves SIO is quiet; act immediately.
                // Ignore while accumulating or sending.
                if (s_accumBuf.empty() && s_escposJob.empty() && s_escposDataMs == 0) {
                    static const uint8_t cut[] = { 0x0A, 0x0A, 0x0A, 0x0A, 0x1D, 0x56, 0x42, 0x00 }; // 4×LF + GS V 66 0 partial cut
                    s_escposJob.assign(cut, cut + sizeof(cut));
                    s_escposOffset = 0;
                    LOG_INFO(TAG, "Button: partial cut queued");
                }
            } else if ((nowMs - sioEmulator.lastActivityMs()) > 1000) {
                PrintJob job = generator->flush();
                if (!job.rawData.empty())
                    manager.submitJob(std::move(job), generator->protocol());
                LOG_INFO(TAG, "Button: label submitted");
            }
        }

        sioEmulator.tick();            // SIO state machine

        // ESC/POS: drain generator into accumulation buffer after every tick.
        // writeLine() appends to the generator's internal buffer; flush() moves it here.
        // We stamp the time so the quiet-timer knows when the last data arrived.
        if (generator->protocol() == ProtocolType::ESCPOS) {
            PrintJob job = generator->flush();
            if (!job.rawData.empty()) {
                s_accumBuf.insert(s_accumBuf.end(), job.rawData.begin(), job.rawData.end());
                s_escposDataMs = nowMs;
            }
        }

        // ESC/POS: when data has been quiet for ESCPOS_FLUSH_MS, send the batch.
        // Using s_escposDataMs (updated only on real data) — not lastActivityMs()
        // which resets on STATUS polls and prevents the timer from ever expiring.
        if (generator->protocol() == ProtocolType::ESCPOS &&
            !s_accumBuf.empty() &&
            s_escposJob.empty() &&
            s_escposDataMs > 0 &&
            (nowMs - s_escposDataMs) > ESCPOS_FLUSH_MS) {
            s_escposJob    = std::move(s_accumBuf);
            s_accumBuf     = {};
            s_escposDataMs = 0;
            s_escposOffset = 0;
        }

        // Drain pending ESC/POS job one chunk per iteration (non-blocking).
        if (!s_escposJob.empty() && !rawTransport->isBusy()) {
            size_t remaining = s_escposJob.size() - s_escposOffset;
            size_t chunk = remaining < RP2040_MAX_PACKET ? remaining : RP2040_MAX_PACKET;
            if (rawTransport->beginWrite(s_escposJob.data() + s_escposOffset, chunk)) {
                s_escposOffset += chunk;
                if (s_escposOffset >= s_escposJob.size()) {
                    s_escposJob.clear();
                    s_escposOffset = 0;
                }
            }
        }

        // ESC ~ P: programmatic print trigger (equivalent to GPIO 8 button)
        if (sioEmulator.takePrintRequest()) {
            if (generator->protocol() == ProtocolType::ESCPOS) {
                PrintJob job = generator->flush();
                if (!job.rawData.empty())
                    s_accumBuf.insert(s_accumBuf.end(), job.rawData.begin(), job.rawData.end());
                if (!s_accumBuf.empty() && s_escposJob.empty()) {
                    s_escposJob    = std::move(s_accumBuf);
                    s_accumBuf     = {};
                    s_escposDataMs = 0;
                    s_escposOffset = 0;
                }
                LOG_INFO(TAG, "ESC~P: ESCPOS batch queued");
            } else {
                PrintJob job = generator->flush();
                if (!job.rawData.empty())
                    manager.submitJob(std::move(job), generator->protocol());
                LOG_INFO(TAG, "ESC~P: label submitted");
            }
        }

        // ESC ~ S n: save current config to flash slot n
        uint8_t flashSlot;
        if (sioEmulator.takeSaveRequest(flashSlot)) {
            if (FlashConfig::save(flashSlot, sioEmulator.getConfig()))
                LOG_INFO(TAG, "ESC~S%u: config saved to flash slot %u", flashSlot, flashSlot);
            else
                LOG_WARN(TAG, "ESC~S%u: flash save failed", flashSlot);
        }

        // ESC ~ R n: load config from flash slot n
        if (sioEmulator.takeLoadRequest(flashSlot)) {
            TextConfig loaded;
            if (FlashConfig::load(flashSlot, loaded)) {
                sioEmulator.setConfig(loaded);
                LOG_INFO(TAG, "ESC~R%u: config loaded from flash slot %u", flashSlot, flashSlot);
            } else {
                LOG_WARN(TAG, "ESC~R%u: no valid config in flash slot %u", flashSlot, flashSlot);
            }
        }

        manager.tick();                // hot-plug polling (no-op on RP2040)
    }
}
#endif // PLATFORM_RP2040
