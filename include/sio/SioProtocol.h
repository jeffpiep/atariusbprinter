#pragma once
#include <cstddef>
#include <cstdint>

namespace Sio {
    constexpr uint32_t BAUD_RATE       = 19200;
    constexpr uint8_t  CMD_WRITE       = 0x57;  // 'W' — normal write (40 bytes)
    constexpr uint8_t  CMD_PUT         = 0x50;  // 'P' — put (graphics, same as WRITE)
    constexpr uint8_t  CMD_STATUS      = 0x53;  // 'S' — query printer status
    constexpr uint8_t  ACK             = 0x41;  // 'A'
    constexpr uint8_t  NAK             = 0x4E;  // 'N'
    constexpr uint8_t  COMPLETE        = 0x43;  // 'C'
    constexpr uint8_t  ERROR_RESP      = 0x45;  // 'E'
    constexpr uint8_t  DEVICE_P1       = 0x40;  // Printer 1
    constexpr uint8_t  DEVICE_P2       = 0x41;  // Printer 2
    constexpr uint8_t  DEVICE_P3       = 0x42;  // Printer 3
    constexpr uint8_t  DEVICE_P4       = 0x43;  // Printer 4
    constexpr uint8_t  RECORD_SIZE     = 40;
    constexpr uint8_t  ATASCII_EOL     = 0x9B;
    constexpr uint32_t CMD_LINE_GPIO   = 6;    // default GPIO for SIO command line
    // Timing (microseconds) per Atari SIO specification
    constexpr uint32_t T_COMPLETE_US   = 250;   // delay before COMPLETE
    constexpr uint32_t T_ACK_US        = 850;   // delay before ACK after command frame

    // Returns the record length for a given AUX1 byte per Atari OS ROM manual:
    //   'N' (0x4E) or default → 40 bytes (normal)
    //   'S' (0x53)            → 29 bytes (sideways, Atari 820)
    //   'D' (0x44)            → 20 bytes (double-wide)
    inline uint8_t recordLength(uint8_t aux1) {
        switch (aux1) {
            case 'S': return 29;
            case 'D': return 20;
            default:  return RECORD_SIZE;
        }
    }

    // Compute standard SIO checksum: sum of bytes mod 256, add carry back.
    uint8_t checksum(const uint8_t* data, size_t len);
} // namespace Sio
