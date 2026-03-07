#pragma once
#include "generator/TextConfig.h"
#include <cstdint>

// Persistent configuration storage in the last 4KB flash sector.
// Supports NUM_SLOTS independent slots (0–9) for different printer setups.
// Triggered by ESC ~ S n (save) and ESC ~ R n (read/load) sequences.
// On boot, main_rp2040.cpp auto-loads slot 0 if a valid config exists.
namespace FlashConfig {

    static constexpr uint8_t NUM_SLOTS = 10;  // slots 0–9

    // Save cfg to slot (0–NUM_SLOTS-1).
    // Returns true on success, false if slot out of range.
    // Interrupts are disabled during erase+program (~20 ms).
    bool save(uint8_t slot, const TextConfig& cfg);

    // Load config from slot into cfg.
    // Returns true if the slot contains a valid (magic+checksum) config.
    // Returns false if slot is out of range, erased, or corrupt.
    bool load(uint8_t slot, TextConfig& cfg);

    // Erase all slots (factory reset of NVM).
    void eraseAll();

} // namespace FlashConfig
