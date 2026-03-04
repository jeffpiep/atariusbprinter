#include "MockUsbTransport.h"
#include <cstring>
#include <algorithm>

MockUsbTransport::MockUsbTransport(MockConfig cfg) : m_cfg(std::move(cfg)) {}

bool MockUsbTransport::open(const UsbDeviceDescriptor&) {
    if (m_cfg.failOnOpen) return false;
    m_open = true;
    return true;
}

void MockUsbTransport::close() {
    m_open = false;
}

int MockUsbTransport::write(const uint8_t* data, size_t len, uint32_t /*timeout_ms*/) {
    if (!m_open) return -1;
    if (m_cfg.failOnWrite) return -1;

    if (m_cfg.disconnectAfter &&
        m_writeCount >= m_cfg.disconnectWriteCount) {
        m_open = false;
        return -1;
    }

    m_written.insert(m_written.end(), data, data + len);
    ++m_writeCount;
    return static_cast<int>(len);
}

int MockUsbTransport::read(uint8_t* buf, size_t maxLen, uint32_t /*timeout_ms*/) {
    if (!m_open) return -1;
    if (m_cfg.writeOnly) return 0;

    if (m_cfg.statusResponse.empty()) return 0;

    size_t n = std::min(maxLen, m_cfg.statusResponse.size());
    memcpy(buf, m_cfg.statusResponse.data(), n);
    return static_cast<int>(n);
}

bool MockUsbTransport::isOpen() const {
    return m_open;
}

void MockUsbTransport::resetCapture() {
    m_written.clear();
    m_writeCount = 0;
}