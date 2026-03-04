#pragma once
#include <cstdint>
#include <cstddef>

class AtasciiConverter {
public:
    // Convert a single ATASCII byte to its closest ASCII equivalent.
    // Returns '?' for characters with no reasonable ASCII mapping.
    static char toAscii(uint8_t atascii);

    // Convert an entire buffer in-place (replaces each byte with its ASCII mapping).
    static void convertBuffer(uint8_t* buf, size_t len);
};
