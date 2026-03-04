#pragma once
#include "transport/UsbDeviceDescriptor.h"
#include "protocol/ProtocolType.h"
#include <cstdint>

struct DeviceRecord {
    UsbDeviceDescriptor descriptor;
    ProtocolType        detectedProtocol = ProtocolType::UNKNOWN;
    bool                active           = false;
    uint32_t            attachTimestamp  = 0; // ms since boot
};