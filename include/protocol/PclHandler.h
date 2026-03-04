#pragma once
#include "protocol/IProtocolHandler.h"

class PclHandler : public IProtocolHandler {
public:
    static constexpr size_t   PCL_CHUNK_SIZE    = 4096;
    static constexpr uint32_t INTER_JOB_DELAY_MS = 100;

    bool probe(const uint8_t* data, size_t len) const override;
    bool sendJob(IUsbTransport& transport, const PrintJob& job) override;
    bool queryStatus(IUsbTransport& transport, std::string& statusOut) override;
    const char* name() const override { return "PCL"; }
};
