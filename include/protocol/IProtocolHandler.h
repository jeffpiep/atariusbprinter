#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include "transport/IUsbTransport.h"
#include "protocol/PrintJob.h"

class IProtocolHandler {
public:
    virtual ~IProtocolHandler() = default;

    // Validate that the supplied data stream begins with a recognized
    // command prefix for this protocol. Used during auto-detection.
    virtual bool probe(const uint8_t* data, size_t len) const = 0;

    // Process and forward a complete print job to the transport.
    // Returns true on success.
    virtual bool sendJob(IUsbTransport& transport, const PrintJob& job) = 0;

    // Query the printer status. Populates statusOut; returns true if
    // a valid status response was received.
    virtual bool queryStatus(IUsbTransport& transport,
                             std::string& statusOut) = 0;

    // Human-readable protocol name for logging.
    virtual const char* name() const = 0;
};