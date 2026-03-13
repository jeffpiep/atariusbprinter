#ifdef PLATFORM_RP2040_USB_TEST
// USB-only smoke test for the POS80 ESC/POS thermal printer.
//
// No SIO, no LineAssembler, no GPIO button — just TinyUSB + Rp2040UsbTransport.
// Sends a fixed sequence of ESC/POS lines with known inter-line delays so you can
// verify the USB write path independently of SIO timing.
//
// Flash as the 'usbprinter_usb_test' cmake target.
// Monitor output via UART (GPIO 0/1, 115200 baud).
//
// Expected result: lines within each group print gap-free; groups with a delay
// above the printer's idle-feed threshold will show a feed gap after the separator.

#include "pico/stdlib.h"
#include "tusb.h"
#include "Rp2040UsbTransport.h"
#include "util/Logger.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "usb_test";

static volatile uint8_t s_pendingMountAddr = 0;
static Rp2040UsbTransport s_transport;

extern "C" {

void tuh_mount_cb(uint8_t devAddr) {
    LOG_INFO(TAG, "USB device mounted: addr=%u", devAddr);
    s_pendingMountAddr = devAddr;
}

void tuh_umount_cb(uint8_t devAddr) {
    LOG_INFO(TAG, "USB device unmounted: addr=%u", devAddr);
    Rp2040UsbTransport::onUnmount(devAddr);
}

} // extern "C"

// Walk the USB configuration descriptor to find the printer-class interface,
// open its bulk endpoints, and register it with Rp2040UsbTransport.
// Mirrors processPendingMount() from main_rp2040.cpp.
static bool processPendingMount(uint8_t devAddr) {
    static uint8_t cfgBuf[512];
    if (tuh_descriptor_get_configuration_sync(devAddr, 0, cfgBuf, sizeof(cfgBuf))
            != XFER_RESULT_SUCCESS) {
        LOG_WARN(TAG, "addr=%u: failed to get config descriptor", devAddr);
        return false;
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
                break;
            }
        }
        p = tu_desc_next(p);
    }

    if (ifaceNum < 0 || epOut == 0 || epOutDesc == nullptr) {
        LOG_INFO(TAG, "addr=%u: no printer-class interface with BULK OUT", devAddr);
        return false;
    }

    if (!tuh_edpt_open(devAddr, epOutDesc)) {
        LOG_ERROR(TAG, "addr=%u: tuh_edpt_open(OUT 0x%02x) failed", devAddr, epOut);
        return false;
    }
    if (hasEpIn && epInDesc != nullptr) {
        if (!tuh_edpt_open(devAddr, epInDesc)) {
            LOG_WARN(TAG, "addr=%u: tuh_edpt_open(IN) failed, treating as write-only", devAddr);
            hasEpIn = false; epIn = 0;
        }
    }

    Rp2040UsbTransport::onMount(devAddr, (uint8_t)ifaceNum, epOut, epIn, hasEpIn);
    LOG_INFO(TAG, "addr=%u: printer ready (epOut=0x%02x)", devAddr, epOut);
    return true;
}

// Build a single ESC/POS line: [ESC 2 on first line] + text + LF
static void buildLine(uint8_t* buf, size_t& len, const char* text, bool setSpacing) {
    len = 0;
    if (setSpacing) {
        buf[len++] = 0x1B;
        buf[len++] = '2'; // ESC 2 — default 1/6-inch line spacing (6 LPI)
    }
    for (const char* p = text; *p; ++p)
        buf[len++] = static_cast<uint8_t>(*p);
    buf[len++] = '\n';
}

int main() {
    stdio_init_all();

    const tusb_rhport_init_t host_init = {TUSB_ROLE_HOST, TUSB_SPEED_AUTO};
    tusb_init(0, &host_init);

    Logger::setLevel(LogLevel::INFO);
    LOG_INFO(TAG, "USB printer smoke test starting");
    LOG_INFO(TAG, "Connect POS80 printer (VID 0483, PID 5720) via USB");

    // Wait for mount callback, then process descriptors and open endpoints
    while (true) {
        tuh_task();
        if (s_pendingMountAddr) {
            uint8_t addr = s_pendingMountAddr;
            s_pendingMountAddr = 0;
            if (processPendingMount(addr)) {
                UsbDeviceDescriptor dev;
                dev.deviceAddress = addr;
                if (s_transport.open(dev)) break;
                LOG_ERROR(TAG, "transport.open failed after mount");
            }
        }
    }
    LOG_INFO(TAG, "Printer opened — starting test sequence");

    // Test series: inter-line delays of 0, 50, 100, 200, 400 ms
    // Each group prints 4 lines; observe which groups have feed gaps.
    static const uint32_t delays_ms[] = {0, 50, 100, 200, 400};
    static const int NUM_DELAYS = 5;
    static const int LINES_PER_GROUP = 4;

    uint8_t lineBuf[64];
    size_t  lineLen;
    char    text[48];
    bool    firstLine = true;

    for (int d = 0; d < NUM_DELAYS; ++d) {
        uint32_t delay = delays_ms[d];
        LOG_INFO(TAG, "--- group delay=%ums ---", (unsigned)delay);

        for (int i = 0; i < LINES_PER_GROUP; ++i) {
            snprintf(text, sizeof(text), "D=%3ums L%d", (unsigned)delay, i + 1);
            buildLine(lineBuf, lineLen, text, firstLine);
            firstLine = false;

            uint32_t t0 = to_ms_since_boot(get_absolute_time());
            int written = s_transport.write(lineBuf, lineLen, 5000);
            uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - t0;

            if (written < 0)
                LOG_ERROR(TAG, "  L%d: write failed", i + 1);
            else
                LOG_INFO(TAG, "  L%d: %d bytes in %ums", i + 1, written, (unsigned)elapsed);

            if (delay > 0) sleep_ms(delay);
        }

        // Separator between groups (no spacing command — uses current state)
        snprintf(text, sizeof(text), "--- end %ums ---", (unsigned)delay);
        buildLine(lineBuf, lineLen, text, false);
        s_transport.write(lineBuf, lineLen, 5000);
        sleep_ms(200); // short pause between groups (well under idle-feed threshold)
    }

    LOG_INFO(TAG, "Test complete. Check paper for feed gaps.");
    s_transport.close();

    while (true) {
        tuh_task();
        tight_loop_contents();
    }
}
#endif // PLATFORM_RP2040_USB_TEST
