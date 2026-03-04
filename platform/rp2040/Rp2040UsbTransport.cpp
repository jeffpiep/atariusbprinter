#ifdef PLATFORM_RP2040
#include "Rp2040UsbTransport.h"
#include "util/Logger.h"
#include "pico/time.h"

static const char* TAG = "Rp2040UsbTransport";

// Static storage
Rp2040UsbTransport::DevEntry Rp2040UsbTransport::s_devices[RP2040_MAX_DEVICES];
uint8_t Rp2040UsbTransport::s_txBuf[RP2040_MAX_PACKET];
uint8_t Rp2040UsbTransport::s_rxBuf[RP2040_MAX_PACKET];

// ── Static mount/unmount (called from TinyUSB callbacks) ─────────────────────

void Rp2040UsbTransport::onMount(uint8_t devAddr, uint8_t interfaceNum,
                                  uint8_t epOut, uint8_t epIn, bool hasEpIn) {
    (void)interfaceNum;
    for (int i = 0; i < RP2040_MAX_DEVICES; ++i) {
        if (!s_devices[i].active) {
            s_devices[i].devAddr  = devAddr;
            s_devices[i].epOut    = epOut;
            s_devices[i].epIn     = epIn;
            s_devices[i].hasEpIn  = hasEpIn;
            s_devices[i].active   = true;
            LOG_INFO(TAG, "Device mounted: addr=%u epOut=0x%02x", devAddr, epOut);
            return;
        }
    }
    LOG_WARN(TAG, "Device table full, ignoring addr=%u", devAddr);
}

void Rp2040UsbTransport::onUnmount(uint8_t devAddr) {
    for (int i = 0; i < RP2040_MAX_DEVICES; ++i) {
        if (s_devices[i].active && s_devices[i].devAddr == devAddr) {
            s_devices[i].active = false;
            LOG_INFO(TAG, "Device unmounted: addr=%u", devAddr);
            return;
        }
    }
}

// ── IUsbTransport ─────────────────────────────────────────────────────────────

bool Rp2040UsbTransport::open(const UsbDeviceDescriptor& dev) {
    // Find a mounted device matching this descriptor (by bus/addr or first available)
    for (int i = 0; i < RP2040_MAX_DEVICES; ++i) {
        if (s_devices[i].active &&
            s_devices[i].devAddr == dev.deviceAddress) {
            m_devAddr  = s_devices[i].devAddr;
            m_epOut    = s_devices[i].epOut;
            m_epIn     = s_devices[i].epIn;
            m_hasEpIn  = s_devices[i].hasEpIn;
            m_open     = true;
            LOG_INFO(TAG, "Opened addr=%u", m_devAddr);
            return true;
        }
    }
    // Fallback: use first available
    for (int i = 0; i < RP2040_MAX_DEVICES; ++i) {
        if (s_devices[i].active) {
            m_devAddr  = s_devices[i].devAddr;
            m_epOut    = s_devices[i].epOut;
            m_epIn     = s_devices[i].epIn;
            m_hasEpIn  = s_devices[i].hasEpIn;
            m_open     = true;
            LOG_INFO(TAG, "Opened (fallback) addr=%u", m_devAddr);
            return true;
        }
    }
    LOG_ERROR(TAG, "open: no mounted device available");
    return false;
}

void Rp2040UsbTransport::close() {
    m_open = false;
    LOG_INFO(TAG, "Device closed");
}

bool Rp2040UsbTransport::isOpen() const {
    return m_open;
}

// Transfer completion callback
void Rp2040UsbTransport::transferCallback(tuh_xfer_t* xfer) {
    auto* self = reinterpret_cast<Rp2040UsbTransport*>(xfer->user_data);
    self->m_txResult = (xfer->result == XFER_RESULT_SUCCESS)
                       ? static_cast<int32_t>(xfer->actual_len)
                       : -1;
    self->m_txDone = true;
}

int Rp2040UsbTransport::write(const uint8_t* data, size_t len, uint32_t timeout_ms) {
    if (!m_open) { LOG_ERROR(TAG, "write: not open"); return -1; }

    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > RP2040_MAX_PACKET) chunk = RP2040_MAX_PACKET;

        // Copy into static tx buffer (no heap)
        for (size_t i = 0; i < chunk; ++i) s_txBuf[i] = data[offset + i];

        m_txDone   = false;
        m_txResult = 0;

        tuh_xfer_t xfer = {};
        xfer.daddr      = m_devAddr;
        xfer.ep_addr    = m_epOut;
        xfer.buffer     = s_txBuf;
        xfer.buflen     = static_cast<uint16_t>(chunk);
        xfer.complete_cb = &Rp2040UsbTransport::transferCallback;
        xfer.user_data  = reinterpret_cast<uintptr_t>(this);

        if (!tuh_edpt_xfer(&xfer)) {
            LOG_ERROR(TAG, "write: tuh_edpt_xfer failed");
            return -1;
        }

        // Poll until done or timeout
        uint32_t deadline = to_ms_since_boot(get_absolute_time()) + timeout_ms;
        while (!m_txDone) {
            tuh_task();
            if (to_ms_since_boot(get_absolute_time()) > deadline) {
                LOG_WARN(TAG, "write: timeout");
                return -1;
            }
        }

        if (m_txResult < 0) {
            LOG_ERROR(TAG, "write: transfer error");
            return -1;
        }
        offset += static_cast<size_t>(m_txResult);
    }
    return static_cast<int>(offset);
}

int Rp2040UsbTransport::read(uint8_t* buf, size_t maxLen, uint32_t timeout_ms) {
    if (!m_open) { LOG_ERROR(TAG, "read: not open"); return -1; }
    if (!m_hasEpIn) return 0;

    size_t chunk = maxLen < RP2040_MAX_PACKET ? maxLen : RP2040_MAX_PACKET;
    m_txDone   = false;
    m_txResult = 0;

    tuh_xfer_t xfer = {};
    xfer.daddr      = m_devAddr;
    xfer.ep_addr    = m_epIn;
    xfer.buffer     = s_rxBuf;
    xfer.buflen     = static_cast<uint16_t>(chunk);
    xfer.complete_cb = &Rp2040UsbTransport::transferCallback;
    xfer.user_data  = reinterpret_cast<uintptr_t>(this);

    if (!tuh_edpt_xfer(&xfer)) return 0;

    uint32_t deadline = to_ms_since_boot(get_absolute_time()) + timeout_ms;
    while (!m_txDone) {
        tuh_task();
        if (to_ms_since_boot(get_absolute_time()) > deadline) return 0;
    }

    if (m_txResult <= 0) return 0;
    size_t n = static_cast<size_t>(m_txResult);
    for (size_t i = 0; i < n; ++i) buf[i] = s_rxBuf[i];
    return static_cast<int>(n);
}

#endif // PLATFORM_RP2040
