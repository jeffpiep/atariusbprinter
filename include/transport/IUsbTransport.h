#pragma once
#include <cstdint>
#include <cstddef>
#include "transport/UsbDeviceDescriptor.h"

class IUsbTransport {
public:
    virtual ~IUsbTransport() = default;

    // Open a specific device. Returns false on failure.
    virtual bool open(const UsbDeviceDescriptor& dev) = 0;

    // Close the currently open device.
    virtual void close() = 0;

    // Blocking bulk write to the printer OUT endpoint.
    // Returns number of bytes written, or -1 on error.
    virtual int write(const uint8_t* data, size_t len,
                      uint32_t timeout_ms = 2000) = 0;

    // Blocking bulk read from the printer IN endpoint (status).
    // Returns number of bytes read, or -1 on error.
    virtual int read(uint8_t* buf, size_t maxLen,
                     uint32_t timeout_ms = 500) = 0;

    // Query device open state.
    virtual bool isOpen() const = 0;
};
