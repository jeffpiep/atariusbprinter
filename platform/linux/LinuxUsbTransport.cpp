#include "LinuxUsbTransport.h"
#include "util/Logger.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "LinuxUsbTransport";
static constexpr uint8_t USB_CLASS_PRINTER = 0x07;

// ── Construction / destruction ───────────────────────────────────────────────

LinuxUsbTransport::LinuxUsbTransport() {
    int r = libusb_init(&m_ctx);
    if (r < 0) {
        LOG_ERROR(TAG, "libusb_init failed: %s", libusb_error_name(r));
        m_ctx = nullptr;
    }
}

LinuxUsbTransport::~LinuxUsbTransport() {
    stopHotplug();
    close();
    if (m_ctx) {
        libusb_exit(m_ctx);
        m_ctx = nullptr;
    }
}

// ── Error mapping ─────────────────────────────────────────────────────────────

TransportError LinuxUsbTransport::mapLibusbError(int err) {
    switch (err) {
        case LIBUSB_ERROR_TIMEOUT:   return TransportError::Timeout;
        case LIBUSB_ERROR_NO_DEVICE: return TransportError::Disconnected;
        case LIBUSB_ERROR_IO:        return TransportError::Disconnected;
        case LIBUSB_ERROR_ACCESS:    return TransportError::AccessDenied;
        case LIBUSB_ERROR_PIPE:      return TransportError::Disconnected;
        default:                     return TransportError::Unknown;
    }
}

// ── Interface claiming ────────────────────────────────────────────────────────

bool LinuxUsbTransport::claimPrinterInterface() {
    libusb_device* dev = libusb_get_device(m_handle);
    libusb_config_descriptor* config = nullptr;

    int r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0) {
        LOG_ERROR(TAG, "get_active_config_descriptor: %s", libusb_error_name(r));
        return false;
    }

    bool found = false;
    for (int i = 0; i < config->bNumInterfaces && !found; ++i) {
        const libusb_interface& iface = config->interface[i];
        for (int a = 0; a < iface.num_altsetting && !found; ++a) {
            const libusb_interface_descriptor& desc = iface.altsetting[a];
            if (desc.bInterfaceClass != USB_CLASS_PRINTER) continue;

            m_interfaceNum = desc.bInterfaceNumber;
            m_epOut = 0;
            m_epIn  = 0;
            m_hasEpIn = false;

            for (int e = 0; e < desc.bNumEndpoints; ++e) {
                const libusb_endpoint_descriptor& ep = desc.endpoint[e];
                bool isIn   = (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0;
                bool isBulk = (ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
                               == LIBUSB_TRANSFER_TYPE_BULK;
                if (!isBulk) continue;
                if (isIn && !m_hasEpIn) {
                    m_epIn = ep.bEndpointAddress;
                    m_hasEpIn = true;
                } else if (!isIn && m_epOut == 0) {
                    m_epOut = ep.bEndpointAddress;
                }
            }

            if (m_epOut == 0) {
                LOG_WARN(TAG, "Interface %d has no BULK OUT endpoint, skipping", i);
                continue;
            }
            found = true;
        }
    }
    libusb_free_config_descriptor(config);

    if (!found) {
        LOG_ERROR(TAG, "No printer-class interface with BULK OUT found");
        return false;
    }

    libusb_set_auto_detach_kernel_driver(m_handle, 1);

    r = libusb_claim_interface(m_handle, m_interfaceNum);
    if (r < 0) {
        m_lastError = mapLibusbError(r);
        LOG_ERROR(TAG, "claim_interface(%d): %s", m_interfaceNum, libusb_error_name(r));
        return false;
    }

    LOG_INFO(TAG, "Claimed interface %d (epOut=0x%02x epIn=0x%02x hasEpIn=%d)",
             m_interfaceNum, m_epOut, m_epIn, (int)m_hasEpIn);
    return true;
}

void LinuxUsbTransport::releaseInterface() {
    if (m_handle && m_interfaceNum >= 0) {
        libusb_release_interface(m_handle, m_interfaceNum);
        m_interfaceNum = -1;
    }
}

// ── IUsbTransport ─────────────────────────────────────────────────────────────

bool LinuxUsbTransport::open(const UsbDeviceDescriptor& dev) {
    if (!m_ctx) return false;
    if (m_handle) close();

    m_handle = libusb_open_device_with_vid_pid(m_ctx, dev.vendorId, dev.productId);
    if (!m_handle) {
        m_lastError = TransportError::AccessDenied;
        LOG_ERROR(TAG, "Device %04x:%04x not found or cannot be opened",
                  dev.vendorId, dev.productId);
        return false;
    }

    if (!claimPrinterInterface()) {
        libusb_close(m_handle);
        m_handle = nullptr;
        return false;
    }

    LOG_INFO(TAG, "Opened %s", dev.description().c_str());
    m_lastError = TransportError::None;
    return true;
}

void LinuxUsbTransport::close() {
    releaseInterface();
    if (m_handle) {
        libusb_close(m_handle);
        m_handle = nullptr;
        LOG_INFO(TAG, "Device closed");
    }
}

int LinuxUsbTransport::write(const uint8_t* data, size_t len, uint32_t timeout_ms) {
    if (!m_handle || m_epOut == 0) {
        LOG_ERROR(TAG, "write: device not open");
        return -1;
    }
    int transferred = 0;
    int r = libusb_bulk_transfer(m_handle, m_epOut,
                                 const_cast<uint8_t*>(data),
                                 static_cast<int>(len),
                                 &transferred, timeout_ms);
    if (r < 0) {
        m_lastError = mapLibusbError(r);
        LOG_WARN(TAG, "bulk_transfer write: %s (transferred=%d)",
                 libusb_error_name(r), transferred);
        return -1;
    }
    return transferred;
}

int LinuxUsbTransport::read(uint8_t* buf, size_t maxLen, uint32_t timeout_ms) {
    if (!m_handle) {
        LOG_ERROR(TAG, "read: device not open");
        return -1;
    }
    if (!m_hasEpIn) return 0; // write-only, non-fatal

    int transferred = 0;
    int r = libusb_bulk_transfer(m_handle, m_epIn, buf,
                                 static_cast<int>(maxLen),
                                 &transferred, timeout_ms);
    if (r < 0 && r != LIBUSB_ERROR_TIMEOUT) {
        m_lastError = mapLibusbError(r);
        LOG_WARN(TAG, "bulk_transfer read: %s", libusb_error_name(r));
        return -1;
    }
    if (r == LIBUSB_ERROR_TIMEOUT) return 0;
    return transferred;
}

bool LinuxUsbTransport::isOpen() const {
    return m_handle != nullptr;
}

// ── IHotplugCapable ───────────────────────────────────────────────────────────

bool LinuxUsbTransport::startHotplug(HotplugCb cb) {
    if (!m_ctx) return false;
    if (m_eventRunning) stopHotplug();

    m_hotplugCb = std::move(cb);

    if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        LOG_WARN(TAG, "hotplug not supported on this platform");
        return false;
    }

    int r = libusb_hotplug_register_callback(
        m_ctx,
        static_cast<libusb_hotplug_event>(
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
            LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
        LIBUSB_HOTPLUG_ENUMERATE,      // fire for already-attached devices
        LIBUSB_HOTPLUG_MATCH_ANY,      // any vendor
        LIBUSB_HOTPLUG_MATCH_ANY,      // any product
        LIBUSB_HOTPLUG_MATCH_ANY,      // any class (checked in callback)
        &LinuxUsbTransport::hotplugCallback,
        this,
        &m_hotplugHandle);

    if (r != LIBUSB_SUCCESS) {
        LOG_ERROR(TAG, "hotplug_register_callback: %s", libusb_error_name(r));
        return false;
    }

    m_eventRunning = true;
    m_eventThread = std::thread([this]() {
        while (m_eventRunning.load(std::memory_order_relaxed)) {
            struct timeval tv = {0, 100000}; // 100 ms
            libusb_handle_events_timeout(m_ctx, &tv);
        }
    });

    LOG_INFO(TAG, "Hotplug monitoring started");
    return true;
}

void LinuxUsbTransport::stopHotplug() {
    if (!m_eventRunning) return;
    m_eventRunning = false;
    if (m_eventThread.joinable()) m_eventThread.join();
    if (m_ctx && m_hotplugHandle) {
        libusb_hotplug_deregister_callback(m_ctx, m_hotplugHandle);
        m_hotplugHandle = 0;
    }
    LOG_INFO(TAG, "Hotplug monitoring stopped");
}

// static
void LinuxUsbTransport::fillDescriptor(libusb_device* dev,
                                       const libusb_device_descriptor& ddesc,
                                       UsbDeviceDescriptor& out) {
    out.vendorId      = ddesc.idVendor;
    out.productId     = ddesc.idProduct;
    out.busNumber     = libusb_get_bus_number(dev);
    out.deviceAddress = libusb_get_device_address(dev);

    libusb_device_handle* handle = nullptr;
    int openRet = libusb_open(dev, &handle);
    if (openRet == 0) {
        char buf[128];
        if (ddesc.iManufacturer &&
            libusb_get_string_descriptor_ascii(handle, ddesc.iManufacturer,
                reinterpret_cast<uint8_t*>(buf), sizeof(buf)) > 0)
            out.manufacturer = buf;
        if (ddesc.iProduct &&
            libusb_get_string_descriptor_ascii(handle, ddesc.iProduct,
                reinterpret_cast<uint8_t*>(buf), sizeof(buf)) > 0)
            out.product = buf;
        if (ddesc.iSerialNumber &&
            libusb_get_string_descriptor_ascii(handle, ddesc.iSerialNumber,
                reinterpret_cast<uint8_t*>(buf), sizeof(buf)) > 0)
            out.serialNumber = buf;
        libusb_close(handle);
    } else {
        LOG_DEBUG(TAG, "fillDescriptor: cannot open %04x:%04x to read strings: %s "
                  "(add a udev rule for this VID or use a class-based rule)",
                  out.vendorId, out.productId, libusb_error_name(openRet));
    }

#ifdef PLATFORM_LINUX
    // Sysfs fallback: read kernel-cached strings from /sys/bus/usb/devices/...
    // These are world-readable and do not require USB device permissions.
    if (out.manufacturer.empty() || out.product.empty()) {
        uint8_t ports[7];
        int numPorts = libusb_get_port_numbers(dev, ports, 7);
        if (numPorts > 0) {
            char sysDir[256];
            int n = snprintf(sysDir, sizeof(sysDir), "/sys/bus/usb/devices/%u-%u",
                             out.busNumber, ports[0]);
            for (int p = 1; p < numPorts && n < (int)sizeof(sysDir) - 4; ++p)
                n += snprintf(sysDir + n, sizeof(sysDir) - n, ".%u", ports[p]);

            auto readSysStr = [&](const char* file, std::string& dest) {
                if (!dest.empty()) return;
                char path[300];
                snprintf(path, sizeof(path), "%s/%s", sysDir, file);
                FILE* f = fopen(path, "r");
                if (!f) return;
                char buf[128] = {};
                if (fgets(buf, sizeof(buf), f)) {
                    size_t len = strlen(buf);
                    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
                        buf[--len] = '\0';
                    dest = buf;
                }
                fclose(f);
            };

            readSysStr("manufacturer", out.manufacturer);
            readSysStr("product",      out.product);

            if (!out.manufacturer.empty() || !out.product.empty())
                LOG_DEBUG(TAG, "fillDescriptor: used sysfs fallback for %04x:%04x",
                          out.vendorId, out.productId);
        }
    }
#endif
}

// static
int LIBUSB_CALL LinuxUsbTransport::hotplugCallback(libusb_context*,
                                                    libusb_device* dev,
                                                    libusb_hotplug_event event,
                                                    void* userData) {
    auto* self = static_cast<LinuxUsbTransport*>(userData);
    if (!self->m_hotplugCb) return 0;

    libusb_device_descriptor ddesc;
    if (libusb_get_device_descriptor(dev, &ddesc) != LIBUSB_SUCCESS) return 0;

    // Filter: check for printer-class interface
    bool isPrinter = (ddesc.bDeviceClass == USB_CLASS_PRINTER);
    if (!isPrinter) {
        libusb_config_descriptor* cfg = nullptr;
        if (libusb_get_active_config_descriptor(dev, &cfg) == 0) {
            for (int i = 0; i < cfg->bNumInterfaces && !isPrinter; ++i) {
                for (int a = 0; a < cfg->interface[i].num_altsetting; ++a) {
                    if (cfg->interface[i].altsetting[a].bInterfaceClass
                        == USB_CLASS_PRINTER) {
                        isPrinter = true; break;
                    }
                }
            }
            libusb_free_config_descriptor(cfg);
        }
    }
    if (!isPrinter) return 0;

    bool attached = (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);

    UsbDeviceDescriptor udesc;
    if (attached) {
        fillDescriptor(dev, ddesc, udesc);
    } else {
        udesc.vendorId      = ddesc.idVendor;
        udesc.productId     = ddesc.idProduct;
        udesc.busNumber     = libusb_get_bus_number(dev);
        udesc.deviceAddress = libusb_get_device_address(dev);
    }

    LOG_INFO(TAG, "Hotplug %s: %04x:%04x bus%u addr%u",
             attached ? "ATTACH" : "DETACH",
             udesc.vendorId, udesc.productId, udesc.busNumber, udesc.deviceAddress);

    self->m_hotplugCb(udesc, attached);
    return 0;
}

// ── Enumerate ─────────────────────────────────────────────────────────────────

int LinuxUsbTransport::enumeratePrinters(UsbDeviceDescriptor* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;

    libusb_context* ctx = nullptr;
    int r = libusb_init(&ctx);
    if (r < 0) return -1;

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) { libusb_exit(ctx); return -1; }

    int found = 0;
    for (ssize_t i = 0; i < count && found < maxCount; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) < 0) continue;

        bool isPrinter = (desc.bDeviceClass == USB_CLASS_PRINTER);
        if (!isPrinter) {
            libusb_config_descriptor* cfg = nullptr;
            if (libusb_get_active_config_descriptor(dev, &cfg) == 0) {
                for (int j = 0; j < cfg->bNumInterfaces && !isPrinter; ++j) {
                    for (int a = 0; a < cfg->interface[j].num_altsetting; ++a) {
                        if (cfg->interface[j].altsetting[a].bInterfaceClass
                            == USB_CLASS_PRINTER) { isPrinter = true; break; }
                    }
                }
                libusb_free_config_descriptor(cfg);
            }
        }
        if (!isPrinter) continue;

        fillDescriptor(dev, desc, out[found]);
        ++found;
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return found;
}
