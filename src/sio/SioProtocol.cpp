#include "sio/SioProtocol.h"
#include "sio/SioCommandFrame.h"

namespace Sio {

uint8_t checksum(const uint8_t* data, size_t len) {
    uint16_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
        if (sum >= 256) {
            sum -= 255; // add back the carry (same as (sum & 0xFF) + (sum >> 8))
        }
    }
    return static_cast<uint8_t>(sum);
}

} // namespace Sio

bool SioCommandFrame::isValid() const {
    uint8_t data[4] = {deviceId, command, aux1, aux2};
    return checksum == Sio::checksum(data, 4);
}

bool SioCommandFrame::isPrinterDevice() const {
    return deviceId >= Sio::DEVICE_P1 && deviceId <= Sio::DEVICE_P4;
}
