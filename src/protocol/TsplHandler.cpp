#include "protocol/TsplHandler.h"
#include "util/Logger.h"
#include <cstring>

static const char* TAG = "TsplHandler";

bool TsplHandler::probe(const uint8_t* data, size_t len) const {
    if (!data || len == 0) return false;

    // Check for TSPL SIZE command
    if (len >= 4 && memcmp(data, "SIZE", 4) == 0) return true;

    // Check for ZPL preamble ^XA
    if (len >= 3 && memcmp(data, "^XA", 3) == 0) return true;

    // Check for TSPL ESC-@ reset (0x1B 0x40)
    if (len >= 2 && data[0] == 0x1B && data[1] == 0x40) return true;

    return false;
}

bool TsplHandler::sendJob(IUsbTransport& transport, const PrintJob& job) {
    if (job.rawData.empty()) {
        LOG_WARN(TAG, "sendJob: empty job '%s'", job.jobId.c_str());
        return true;
    }

    const uint8_t* src    = job.rawData.data();
    size_t         total  = job.rawData.size();
    size_t         offset = 0;

    LOG_INFO(TAG, "sendJob '%s': %zu bytes in chunks of %zu",
             job.jobId.c_str(), total, TSPL_CHUNK_SIZE);

    while (offset < total) {
        size_t chunkLen = total - offset;
        if (chunkLen > TSPL_CHUNK_SIZE) chunkLen = TSPL_CHUNK_SIZE;

        int written = transport.write(src + offset, chunkLen, job.timeoutMs);
        if (written < 0) {
            LOG_ERROR(TAG, "write failed at offset %zu", offset);
            return false;
        }
        offset += static_cast<size_t>(written);
    }

    LOG_INFO(TAG, "sendJob '%s': complete", job.jobId.c_str());
    return true;
}

bool TsplHandler::queryStatus(IUsbTransport& transport, std::string& statusOut) {
    // Send TSPL ~HS (host status) query
    static const uint8_t HS_CMD[] = "~HS\r\n";
    int r = transport.write(HS_CMD, sizeof(HS_CMD) - 1, 1000);
    if (r < 0) {
        LOG_WARN(TAG, "queryStatus: write ~HS failed");
        return false;
    }

    uint8_t buf[256];
    int read = transport.read(buf, sizeof(buf), 1000);
    if (read <= 0) {
        // Non-fatal — many label printers have no IN endpoint
        LOG_DEBUG(TAG, "queryStatus: no response (write-only device?)");
        statusOut.clear();
        return false;
    }

    statusOut.assign(reinterpret_cast<char*>(buf), static_cast<size_t>(read));
    LOG_DEBUG(TAG, "queryStatus: '%s'", statusOut.c_str());
    return true;
}