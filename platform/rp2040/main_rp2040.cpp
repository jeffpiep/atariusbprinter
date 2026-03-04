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

    // Printer manager owns the transport (heap-allocated, never freed)
    static PrinterManager manager(std::make_unique<Rp2040UsbTransport>());
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
    auto generator = makeTextGenerator(
#if   ATARI_PRINTER_PERSONALITY == 1025
        ProtocolType::PCL
#elif ATARI_PRINTER_PERSONALITY == TSPL
        ProtocolType::TSPL
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

    static SioPrinterEmulator sioEmulator(sioUart, *generator, mode);

    LOG_INFO(TAG, "Entering main loop");

    static uint32_t s_lastButtonMs = 0;

    while (true) {
        tuh_task();                    // TinyUSB host tick

        if (s_pendingMountAddr) {
            uint8_t addr = s_pendingMountAddr;
            s_pendingMountAddr = 0;
            processPendingMount(addr, manager);
        }

        // GPIO 8 (active-low): user signals end of print job.
        // Guards:
        //   - 5-second debounce window avoids multiple triggering of flush().
        //   - 2-second SIO-quiet guard refuses presses while the Atari is
        //     actively printing; the ~37ms USB write would block tick() and
        //     cause the next SIO command to time out (Error 138).
        uint32_t nowMs = to_ms_since_boot(get_absolute_time());
        if (!gpio_get(8)
            && (nowMs - s_lastButtonMs) > 5000
            && (nowMs - sioEmulator.lastActivityMs()) > 2000) {
            s_lastButtonMs = nowMs;
            PrintJob job = generator->flush();
            if (!job.rawData.empty())
                manager.submitJob(std::move(job), ProtocolType::TSPL);
            LOG_INFO(TAG, "Button: label submitted");
        }

        sioEmulator.tick();            // SIO state machine
        manager.tick();                // hot-plug polling (no-op on RP2040)
    }
}
#endif // PLATFORM_RP2040
