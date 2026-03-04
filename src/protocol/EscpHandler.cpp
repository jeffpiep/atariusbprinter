#include "protocol/EscpHandler.h"
#include "util/Logger.h"
#include <cstring>
#include <thread>
#include <chrono>

static const char* TAG = "EscpHandler";

bool EscpHandler::probe(const uint8_t* data, size_t len) const {
    if (!data || len == 0) return false;

    // ESC @ (ESC/P initialize) or ESC P (select printer) within first 8 bytes
    for (size_t i = 0; i + 1 < len && i < 8; ++i) {
        if (data[i] == 0x1B) {
            if (data[i+1] == '@' || data[i+1] == 0x50) return true;
        }
    }

    // GS class command (ESC/POS receipt printers) — first byte
    if (data[0] == 0x1D) return true;

    return false;
}

bool EscpHandler::sendJob(IUsbTransport& transport, const PrintJob& job) {
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
        if (chunk > ESCP_CHUNK_SIZE) chunk = ESCP_CHUNK_SIZE;

        int written = transport.write(src + offset, chunk, job.timeoutMs);
        if (written < 0) {
            LOG_ERROR(TAG, "write failed at offset %zu", offset);
            return false;
        }
        offset += static_cast<size_t>(written);

        if (ESCP_LINE_DELAY_MS > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(ESCP_LINE_DELAY_MS));
        }
    }

    LOG_INFO(TAG, "sendJob '%s': complete", job.jobId.c_str());
    return true;
}

bool EscpHandler::queryStatus(IUsbTransport& transport, std::string& statusOut) {
    // ESC/POS real-time status request (DLE EOT n)
    static const uint8_t RT_STATUS[] = {0x1B, 'v'};
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
