#pragma once
#include "sio/SioProtocol.h"
#include <cstdint>

struct SioCommandFrame {
    uint8_t deviceId;
    uint8_t command;
    uint8_t aux1;
    uint8_t aux2;
    uint8_t checksum;

    // Returns true if the checksum byte matches sum(deviceId..aux2) SIO-style.
    bool isValid() const;

    // Returns true if deviceId is in the printer device range $40–$43.
    bool isPrinterDevice() const;
};
