#pragma once
#include "protocol/IProtocolHandler.h"

class EscposHandler : public IProtocolHandler {
public:
    static constexpr size_t   ESCPOS_CHUNK_SIZE    = 4096;
    static constexpr uint32_t INTER_JOB_DELAY_MS   = 0;

    bool probe(const uint8_t* data, size_t len) const override;
    bool sendJob(IUsbTransport& transport, const PrintJob& job) override;
    bool queryStatus(IUsbTransport& transport, std::string& statusOut) override;
    const char* name() const override { return "ESCPOS"; }
};
