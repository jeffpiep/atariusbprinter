#ifdef PLATFORM_RP2040
#include "FlashConfig.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <cstring>

// ── Flash layout ───────────────────────────────────────────────────────────
//
// We use the last 4KB sector of flash (one erasable unit).
// Each slot occupies 32 bytes; 10 slots = 320 bytes, rest is unused (0xFF).
//
// Offset within sector:  slot_num * sizeof(FlashSlot)
//
// FlashSlot (32 bytes, packed):
//   [0..3]   magic    = MAGIC_BASE | slot_num
//   [4..5]   labelWidthDots
//   [6..7]   labelHeightDots
//   [8]      gapDots
//   [9..10]  marginLeftDots
//   [11..12] marginTopDots
//   [13]     tsplFontId
//   [14]     tsplFontXMul
//   [15]     tsplFontYMul
//   [16]     pclLinesPerPage
//   [17]     pclCpi
//   [18]     pclLpi
//   [19]     escpCondensed   (0 or 1)
//   [20]     escpLpi
//   [21]     suppressFormFeed (0 or 1)
//   [22..26] _reserved (5 bytes, written as 0)
//   [27]     _pad (1 byte to align checksum to [28])
//   [28..31] checksum = byte sum of [0..27]

static constexpr uint32_t FLASH_OFFSET =
    PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;  // last 4KB sector

static constexpr uint32_t MAGIC_BASE = 0xA7BA5100u;

struct __attribute__((packed)) FlashSlot {
    uint32_t magic;
    uint16_t labelWidthDots;
    uint16_t labelHeightDots;
    uint8_t  gapDots;
    uint16_t marginLeftDots;
    uint16_t marginTopDots;
    uint8_t  tsplFontId;
    uint8_t  tsplFontXMul;
    uint8_t  tsplFontYMul;
    uint8_t  pclLinesPerPage;
    uint8_t  pclCpi;
    uint8_t  pclLpi;
    uint8_t  escpCondensed;
    uint8_t  escpLpi;
    uint8_t  suppressFormFeed;
    uint8_t  _reserved[6];   // pads to 28 bytes before checksum
    uint32_t checksum;        // byte sum of bytes [0..27]
};
static_assert(sizeof(FlashSlot) == 32, "FlashSlot must be 32 bytes");

static uint32_t computeChecksum(const FlashSlot& s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
    uint32_t sum = 0;
    for (size_t i = 0; i < offsetof(FlashSlot, checksum); ++i) {
        sum += p[i];
    }
    return sum;
}

static void serialize(const TextConfig& cfg, uint8_t slot_num, FlashSlot& out) {
    memset(&out, 0, sizeof(out));
    out.magic           = MAGIC_BASE | slot_num;
    out.labelWidthDots  = cfg.labelWidthDots;
    out.labelHeightDots = cfg.labelHeightDots;
    out.gapDots         = cfg.gapDots;
    out.marginLeftDots  = cfg.marginLeftDots;
    out.marginTopDots   = cfg.marginTopDots;
    out.tsplFontId      = cfg.tsplFontId;
    out.tsplFontXMul    = cfg.tsplFontXMul;
    out.tsplFontYMul    = cfg.tsplFontYMul;
    out.pclLinesPerPage = cfg.pclLinesPerPage;
    out.pclCpi          = cfg.pclCpi;
    out.pclLpi          = cfg.pclLpi;
    out.escpCondensed   = cfg.escpCondensed ? 1u : 0u;
    out.escpLpi         = cfg.escpLpi;
    out.suppressFormFeed = cfg.suppressFormFeed ? 1u : 0u;
    out.checksum        = computeChecksum(out);
}

static void deserialize(const FlashSlot& s, TextConfig& cfg) {
    cfg.labelWidthDots   = s.labelWidthDots;
    cfg.labelHeightDots  = s.labelHeightDots;
    cfg.gapDots          = s.gapDots;
    cfg.marginLeftDots   = s.marginLeftDots;
    cfg.marginTopDots    = s.marginTopDots;
    cfg.tsplFontId       = s.tsplFontId;
    cfg.tsplFontXMul     = s.tsplFontXMul;
    cfg.tsplFontYMul     = s.tsplFontYMul;
    cfg.pclLinesPerPage  = s.pclLinesPerPage;
    cfg.pclCpi           = s.pclCpi;
    cfg.pclLpi           = s.pclLpi;
    cfg.escpCondensed    = (s.escpCondensed != 0);
    cfg.escpLpi          = s.escpLpi;
    cfg.suppressFormFeed = (s.suppressFormFeed != 0);
    // jobId is runtime-only; not persisted.
}

// ── Public API ─────────────────────────────────────────────────────────────

bool FlashConfig::save(uint8_t slot, const TextConfig& cfg) {
    if (slot >= NUM_SLOTS) return false;

    // Read entire sector into RAM
    static uint8_t buf[FLASH_SECTOR_SIZE];
    const uint8_t* flash_ptr =
        reinterpret_cast<const uint8_t*>(XIP_BASE + FLASH_OFFSET);
    memcpy(buf, flash_ptr, FLASH_SECTOR_SIZE);

    // Serialize the updated slot
    FlashSlot& s = reinterpret_cast<FlashSlot*>(buf)[slot];
    serialize(cfg, slot, s);

    // Erase sector and re-program — must run with interrupts disabled
    // (RP2040 flash is memory-mapped; XIP must be suspended during writes)
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_OFFSET, buf, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);

    return true;
}

bool FlashConfig::load(uint8_t slot, TextConfig& cfg) {
    if (slot >= NUM_SLOTS) return false;

    const uint8_t* flash_ptr =
        reinterpret_cast<const uint8_t*>(XIP_BASE + FLASH_OFFSET);
    FlashSlot s;
    memcpy(&s, flash_ptr + slot * sizeof(FlashSlot), sizeof(FlashSlot));

    if (s.magic != (MAGIC_BASE | slot)) return false;
    if (s.checksum != computeChecksum(s))  return false;

    deserialize(s, cfg);
    return true;
}

void FlashConfig::eraseAll() {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(FLASH_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}

#endif // PLATFORM_RP2040
