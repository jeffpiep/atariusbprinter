// Linux stub for FlashConfig — no persistent storage on the host.
// ESC ~ S / ESC ~ R flag propagation is still testable via LineAssembler unit
// tests; only the actual flash I/O is stubbed out here.
#include "FlashConfig.h"

bool FlashConfig::save(uint8_t /*slot*/, const TextConfig& /*cfg*/) {
    return false;
}

bool FlashConfig::load(uint8_t /*slot*/, TextConfig& /*cfg*/) {
    return false;
}

void FlashConfig::eraseAll() {
    // no-op on Linux
}
