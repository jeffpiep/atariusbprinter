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
#if ATARI_PRINTER_PERSONALITY == 1028 && __has_include("generator/EscposFontData.h")
#  include "generator/EscposFontData.h"
#  include "generator/EscposTextGenerator.h"
#  define HAVE_ESCPOS_FONT_DATA 1
#endif
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

#if defined(HAVE_ESCPOS_FONT_DATA)
    static_cast<EscposTextGenerator*>(generator.get())
        ->setCustomFont(kEscposFontDownload, kEscposFontDownloadSize);
#endif

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

    // ESC/POS: lines accumulate here after each SIO tick; sent as one blocking
    // write when no new data arrives for ESCPOS_FLUSH_MS.
    // Blocking write (~3ms for a 40-char line) is well within the ~68ms SIO
    // inter-cycle window, so it does not risk SIO Error 138.
    static constexpr uint32_t ESCPOS_FLUSH_MS = 1000;
    static std::vector<uint8_t> s_accumBuf;
    static uint32_t             s_escposDataMs = 0;

#if defined(HAVE_ESCPOS_FONT_DATA)
    // Set on printer attach; consumed one iteration later so tuh_task() gets
    // a clean cycle before we call write() (avoids TinyUSB state conflict).
    static bool s_pendingTestPrint = false;
#endif

    while (true) {
        tuh_task();                    // TinyUSB host tick

        if (s_pendingMountAddr) {
            uint8_t addr = s_pendingMountAddr;
            s_pendingMountAddr = 0;
            processPendingMount(addr, manager);
#if defined(HAVE_ESCPOS_FONT_DATA)
            s_pendingTestPrint = true;  // defer write to next iteration
#endif
        }

#if defined(HAVE_ESCPOS_FONT_DATA)
        if (s_pendingTestPrint) {
            s_pendingTestPrint = false;
            // On attach: reset printer, download custom font, activate it.
            static constexpr uint8_t kInit[]     = { 0x1B, 0x40 };
            static constexpr uint8_t kActivate[] = { 0x1B, 0x25, 0x01 };
            int r = 0;
            r += rawTransport->write(kInit,               sizeof(kInit));
            r += rawTransport->write(kEscposFontDownload, kEscposFontDownloadSize);
            r += rawTransport->write(kActivate,           sizeof(kActivate));
            static_cast<EscposTextGenerator*>(generator.get())->markFontDownloaded();
            LOG_INFO(TAG, "Attach: font download %s (%d bytes)",
                     r > 0 ? "OK" : "FAILED", r);
        }
#endif

        uint32_t nowMs   = to_ms_since_boot(get_absolute_time());
        bool     btnDown = !gpio_get(8);
        bool     btnEdge = btnDown && !s_buttonWasDown
                           && (nowMs - s_lastButtonMs) > 200;
        s_buttonWasDown  = btnDown;

        if (btnEdge) {
            s_lastButtonMs = nowMs;
            if (generator->protocol() == ProtocolType::ESCPOS) {
                if (s_accumBuf.empty() && s_escposDataMs == 0) {
                    static const uint8_t cut[] = {
                        0x0A, 0x0A, 0x0A, 0x0A,       // 4 × LF
                        0x1D, 0x56, 0x42, 0x00         // GS V 66 0 partial cut
                    };
                    rawTransport->write(cut, sizeof(cut));
                    LOG_INFO(TAG, "Button: partial cut sent");
                }
            } else if ((nowMs - sioEmulator.lastActivityMs()) > 1000) {
                PrintJob job = generator->flush();
                if (!job.rawData.empty())
                    manager.submitJob(std::move(job), generator->protocol());
                LOG_INFO(TAG, "Button: label submitted");
            }
        }

        sioEmulator.tick();            // SIO state machine

        // ESC/POS: accumulate lines from generator after each tick.
        if (generator->protocol() == ProtocolType::ESCPOS) {
            PrintJob job = generator->flush();
            if (!job.rawData.empty()) {
                s_accumBuf.insert(s_accumBuf.end(), job.rawData.begin(), job.rawData.end());
                s_escposDataMs = nowMs;
            }
        }

        // ESC/POS: flush accumulated lines once SIO goes quiet for ESCPOS_FLUSH_MS.
        // Using s_escposDataMs (data-only timer) instead of lastActivityMs() which
        // resets on STATUS polls and never goes quiet while P: is open.
        if (generator->protocol() == ProtocolType::ESCPOS &&
            !s_accumBuf.empty() &&
            s_escposDataMs > 0 &&
            (nowMs - s_escposDataMs) > ESCPOS_FLUSH_MS) {
            rawTransport->write(s_accumBuf.data(), s_accumBuf.size());
            s_accumBuf.clear();
            s_escposDataMs = 0;
            LOG_INFO(TAG, "ESC/POS: batch sent");
        }

        // ESC ~ P: immediate print trigger (equivalent to GPIO 8 button)
        if (sioEmulator.takePrintRequest()) {
            if (generator->protocol() == ProtocolType::ESCPOS) {
                PrintJob job = generator->flush();
                if (!job.rawData.empty())
                    s_accumBuf.insert(s_accumBuf.end(), job.rawData.begin(), job.rawData.end());
                if (!s_accumBuf.empty()) {
                    rawTransport->write(s_accumBuf.data(), s_accumBuf.size());
                    s_accumBuf.clear();
                    s_escposDataMs = 0;
                }
                LOG_INFO(TAG, "ESC~P: ESCPOS batch sent");
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
