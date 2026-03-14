#include "protocol/EscposHandler.h"
#include "util/Logger.h"
#include <cstring>

static const char* TAG = "EscposHandler";

bool EscposHandler::probe(const uint8_t* data, size_t len) const {
    if (!data || len == 0) return false;

    // ESC @ (initialize printer) within first 8 bytes
    for (size_t i = 0; i + 1 < len && i < 8; ++i) {
        if (data[i] == 0x1B && data[i+1] == '@') return true;
    }

    // GS class command (ESC/POS receipt printers) — first byte
    if (data[0] == 0x1D) return true;

    return false;
}

bool EscposHandler::sendJob(IUsbTransport& transport, const PrintJob& job) {
    if (job.rawData.empty()) {
        LOG_WARN(TAG, "sendJob: empty job '%s'", job.jobId.c_str());
        return true;
    }

    const uint8_t* src    = job.rawData.data();
    size_t         total  = job.rawData.size();
    size_t         offset = 0;

    LOG_INFO(TAG, "sendJob '%s': %zu bytes", job.jobId.c_str(), total);

    while (offset < total) {
        size_t chunk = total - offset;
        if (chunk > ESCPOS_CHUNK_SIZE) chunk = ESCPOS_CHUNK_SIZE;

        int written = transport.write(src + offset, chunk, job.timeoutMs);
        if (written < 0) {
            LOG_ERROR(TAG, "write failed at offset %zu", offset);
            return false;
        }
        offset += static_cast<size_t>(written);
    }

    LOG_INFO(TAG, "sendJob '%s': complete", job.jobId.c_str());
    return true;
}

bool EscposHandler::queryStatus(IUsbTransport& transport, std::string& statusOut) {
    // ESC/POS real-time status request: DLE EOT 1
    static const uint8_t RT_STATUS[] = {0x10, 0x04, 0x01};
    int r = transport.write(RT_STATUS, sizeof(RT_STATUS), 1000);
    if (r < 0) {
        LOG_WARN(TAG, "queryStatus: write failed");
        return false;
    }

    uint8_t buf[4];
    int read = transport.read(buf, sizeof(buf), 500);
    if (read <= 0) {
        statusOut.clear();
        return false;
    }

    statusOut.assign(reinterpret_cast<char*>(buf), static_cast<size_t>(read));
    LOG_DEBUG(TAG, "queryStatus: %d bytes", read);
    return true;
}
