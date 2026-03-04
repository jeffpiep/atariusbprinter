#include "protocol/PclHandler.h"
#include "util/Logger.h"
#include <cstring>
#include <thread>
#include <chrono>

static const char* TAG = "PclHandler";

bool PclHandler::probe(const uint8_t* data, size_t len) const {
    if (!data || len == 0) return false;

    // PJL universal exit: ESC%-12345X
    if (len >= 9 && data[0] == 0x1B && data[1] == '%' &&
        memcmp(data + 1, "%-12345X", 8) == 0) return true;

    // PCL reset: ESC E within first 16 bytes
    for (size_t i = 0; i + 1 < len && i < 16; ++i) {
        if (data[i] == 0x1B && data[i+1] == 'E') return true;
    }

    return false;
}

bool PclHandler::sendJob(IUsbTransport& transport, const PrintJob& job) {
    if (job.rawData.empty()) {
        LOG_WARN(TAG, "sendJob: empty job '%s'", job.jobId.c_str());
        return true;
    }

    const uint8_t* src   = job.rawData.data();
    size_t         total = job.rawData.size();
    size_t         offset = 0;

    LOG_INFO(TAG, "sendJob '%s': %zu bytes", job.jobId.c_str(), total);

    while (offset < total) {
        size_t chunk = total - offset;
        if (chunk > PCL_CHUNK_SIZE) chunk = PCL_CHUNK_SIZE;

        int written = transport.write(src + offset, chunk, job.timeoutMs);
        if (written < 0) {
            LOG_ERROR(TAG, "write failed at offset %zu", offset);
            return false;
        }
        offset += static_cast<size_t>(written);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_JOB_DELAY_MS));
    LOG_INFO(TAG, "sendJob '%s': complete", job.jobId.c_str());
    return true;
}

bool PclHandler::queryStatus(IUsbTransport& transport, std::string& statusOut) {
    // PJL status query
    static const uint8_t PJL_STATUS[] =
        "\x1b%-12345X@PJL INFO STATUS\r\n\x1b%-12345X";
    int r = transport.write(PJL_STATUS, sizeof(PJL_STATUS) - 1, 1000);
    if (r < 0) {
        LOG_WARN(TAG, "queryStatus: write failed");
        return false;
    }

    uint8_t buf[256];
    int read = transport.read(buf, sizeof(buf), 1000);
    if (read <= 0) {
        statusOut.clear();
        return false;
    }

    statusOut.assign(reinterpret_cast<char*>(buf), static_cast<size_t>(read));
    LOG_DEBUG(TAG, "queryStatus: '%s'", statusOut.c_str());
    return true;
}
