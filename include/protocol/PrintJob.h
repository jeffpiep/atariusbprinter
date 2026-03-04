#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct PrintJob {
    std::vector<uint8_t> rawData;    // Pre-formed command stream
    std::string          jobId;      // Opaque identifier for logging
    uint32_t             timeoutMs = 2000; // Per-job write timeout
};