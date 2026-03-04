#pragma once
#include "transport/IUsbTransport.h"
#include <vector>
#include <cstdint>

// Configurable failure modes
struct MockConfig {
    bool    failOnWrite     = false; // write() always returns -1
    bool    failOnOpen      = false; // open() always returns false
    bool    disconnectAfter = false; // write() returns -1 after disconnectWriteCount writes
    int     disconnectWriteCount = 0;
    bool    writeOnly       = false; // read() returns 0 (no IN endpoint)

    // Status bytes to return from read()
    std::vector<uint8_t> statusResponse;
};

class MockUsbTransport : public IUsbTransport {
public:
    explicit MockUsbTransport(MockConfig cfg = {});

    bool open(const UsbDeviceDescriptor& dev) override;
    void close() override;
    int  write(const uint8_t* data, size_t len, uint32_t timeout_ms = 2000) override;
    int  read(uint8_t* buf, size_t maxLen, uint32_t timeout_ms = 500) override;
    bool isOpen() const override;

    // Inspection helpers
    const std::vector<uint8_t>& writtenBytes() const { return m_written; }
    int  writeCallCount() const { return m_writeCount; }
    void resetCapture();

private:
    MockConfig            m_cfg;
    bool                  m_open       = false;
    std::vector<uint8_t>  m_written;
    int                   m_writeCount = 0;
};