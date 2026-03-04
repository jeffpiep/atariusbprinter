#pragma once
#include <cstdint>
#include <string>

struct UsbDeviceDescriptor {
    uint16_t    vendorId       = 0;
    uint16_t    productId      = 0;
    std::string manufacturer;
    std::string product;
    std::string serialNumber;
    uint8_t     busNumber      = 0;
    uint8_t     deviceAddress  = 0;
    // IEEE 1284 device ID string, if available (used for protocol detection)
    std::string ieee1284Id;

    // Returns a short human-readable description for logging.
    std::string description() const;
};
