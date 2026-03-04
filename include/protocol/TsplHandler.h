#pragma once
#include "protocol/IProtocolHandler.h"

class TsplHandler : public IProtocolHandler {
public:
    static constexpr size_t   TSPL_CHUNK_SIZE    = 4096;

    bool probe(const uint8_t* data, size_t len) const override;
    bool sendJob(IUsbTransport& transport, const PrintJob& job) override;
    bool queryStatus(IUsbTransport& transport, std::string& statusOut) override;
    const char* name() const override { return "TSPL"; }
};