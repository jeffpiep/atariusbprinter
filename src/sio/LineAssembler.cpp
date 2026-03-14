#include "sio/LineAssembler.h"
#include "sio/AtasciiConverter.h"
#include "sio/SioProtocol.h"
#include "protocol/ProtocolType.h"
#include "util/Logger.h"

static const char* TAG = "LineAssembler";

// ── ESC ~ command handler ──────────────────────────────────────────────────
// Applies a single ESC ~ C V command to cfg, sets dirty on any change.
// Format: 0x1B 0x7E <cmd> <val>  (4 bytes, parsed before ATASCII conversion)
//
// Commands:
//   W n  — label width    n mm  (1–255) → n×8 dots
//   H n  — label height   n mm  (1–255) → n×8 dots
//   G n  — gap            n×0.1 mm     → (n×8+5)/10 dots
//   F n  — font ID        1–8
//   X n  — font X mul     1–8
//   Y n  — font Y mul     1–8
//   L n  — left margin    n mm  (0–255) → n×8 dots
//   T n  — top margin     n mm  (0–255) → n×8 dots
//   D 0  — reset to compile-time defaults (val must be 0)
//   R n  — request load from flash slot n (0–9)
//   S n  — request save to flash slot n (0–9)
//   P    — request immediate print (val ignored)
//
static constexpr uint8_t NUM_FLASH_SLOTS = 10;  // slots 0–9

static void applyEscCmd(uint8_t cmd, uint8_t val,
                        TextConfig& cfg,
                        const TextConfig& defaults,
                        bool& dirty,
                        bool& printRequested,
                        bool& saveRequested, uint8_t& saveSlot,
                        bool& loadRequested, uint8_t& loadSlot)
{
    constexpr uint8_t DOTS_PER_MM = 8;
    switch (cmd) {
        case 'W':
            if (val > 0) {
                cfg.labelWidthDots = static_cast<uint16_t>(val) * DOTS_PER_MM;
                dirty = true;
                LOG_INFO(TAG, "ESC~W: labelWidth=%u dots (%u mm)", cfg.labelWidthDots, val);
            } else {
                LOG_INFO(TAG, "ESC~W: val=0 ignored");
            }
            break;
        case 'H':
            if (val > 0) {
                cfg.labelHeightDots = static_cast<uint16_t>(val) * DOTS_PER_MM;
                dirty = true;
                LOG_INFO(TAG, "ESC~H: labelHeight=%u dots (%u mm)", cfg.labelHeightDots, val);
            } else {
                LOG_INFO(TAG, "ESC~H: val=0 ignored");
            }
            break;
        case 'G': {
            // n×0.1 mm → dots = n×8/10, rounded
            uint16_t dots = (static_cast<uint16_t>(val) * DOTS_PER_MM + 5) / 10;
            cfg.gapDots = static_cast<uint8_t>(dots > 255u ? 255u : dots);
            dirty = true;
            LOG_INFO(TAG, "ESC~G: gap=%u dots (%u×0.1 mm)", cfg.gapDots, val);
            break;
        }
        case 'F':
            if (val >= 1 && val <= 8) {
                cfg.tsplFontId = val;
                dirty = true;
                LOG_INFO(TAG, "ESC~F: fontId=%u", val);
            } else {
                LOG_INFO(TAG, "ESC~F: val=%u out of range 1-8, ignored", val);
            }
            break;
        case 'X':
            if (val >= 1 && val <= 8) {
                cfg.tsplFontXMul = val;
                dirty = true;
                LOG_INFO(TAG, "ESC~X: fontXMul=%u", val);
            } else {
                LOG_INFO(TAG, "ESC~X: val=%u out of range 1-8, ignored", val);
            }
            break;
        case 'Y':
            if (val >= 1 && val <= 8) {
                cfg.tsplFontYMul = val;
                dirty = true;
                LOG_INFO(TAG, "ESC~Y: fontYMul=%u", val);
            } else {
                LOG_INFO(TAG, "ESC~Y: val=%u out of range 1-8, ignored", val);
            }
            break;
        case 'L':
            cfg.marginLeftDots = static_cast<uint16_t>(val) * DOTS_PER_MM;
            dirty = true;
            LOG_INFO(TAG, "ESC~L: marginLeft=%u dots (%u mm)", cfg.marginLeftDots, val);
            break;
        case 'T':
            cfg.marginTopDots = static_cast<uint16_t>(val) * DOTS_PER_MM;
            dirty = true;
            LOG_INFO(TAG, "ESC~T: marginTop=%u dots (%u mm)", cfg.marginTopDots, val);
            break;
        case 'D':
            if (val == 0) {
                cfg = defaults;
                dirty = true;
                LOG_INFO(TAG, "ESC~D: config reset to compile-time defaults");
            } else {
                LOG_INFO(TAG, "ESC~D: val=%u != 0, ignored", val);
            }
            break;
        case 'R':
            if (val < NUM_FLASH_SLOTS) {
                loadRequested = true;
                loadSlot = val;
                LOG_INFO(TAG, "ESC~R%u: load from flash slot %u requested", val, val);
            } else {
                LOG_WARN(TAG, "ESC~R%u: slot out of range (max %u), ignored",
                         val, NUM_FLASH_SLOTS - 1);
            }
            break;
        case 'S':
            if (val < NUM_FLASH_SLOTS) {
                saveRequested = true;
                saveSlot = val;
                LOG_INFO(TAG, "ESC~S%u: save to flash slot %u requested", val, val);
            } else {
                LOG_WARN(TAG, "ESC~S%u: slot out of range (max %u), ignored",
                         val, NUM_FLASH_SLOTS - 1);
            }
            break;
        case 'P':
            // Trigger immediate print — value byte is ignored.
            printRequested = true;
            LOG_INFO(TAG, "ESC~P: print requested");
            break;
        default:
            LOG_INFO(TAG, "ESC~%c (0x%02x): unknown command, ignored", cmd, cmd);
            break;
    }
}

bool LineAssembler::takePrintRequest() {
    bool r = m_printRequested;
    m_printRequested = false;
    return r;
}

bool LineAssembler::takeSaveRequest(uint8_t& slot) {
    if (!m_saveRequested) return false;
    slot = m_saveSlot;
    m_saveRequested = false;
    return true;
}

bool LineAssembler::takeLoadRequest(uint8_t& slot) {
    if (!m_loadRequested) return false;
    slot = m_loadSlot;
    m_loadRequested = false;
    return true;
}

void LineAssembler::setConfig(const TextConfig& cfg) {
    m_config = cfg;
    m_configDirty = true;
}

const TextConfig& LineAssembler::getConfig() const {
    return m_config;
}

// ── LineAssembler ──────────────────────────────────────────────────────────

LineAssembler::LineAssembler(Mode mode,
                             ITextGenerator& generator,
                             const TextConfig& defaultConfig)
    : m_mode(mode)
    , m_generator(&generator)
    , m_defaultConfig(defaultConfig)
    , m_config(defaultConfig)
{}

void LineAssembler::setGenerator(ITextGenerator& gen) {
    m_generator = &gen;
}

void LineAssembler::flushConfigIfDirty() {
    if (m_configDirty && m_generator->protocol() == ProtocolType::TSPL) {
        m_generator->configure(m_config);
        m_configDirty = false;
    }
}

std::string LineAssembler::processRecord(const uint8_t* record, uint8_t len) {
    // 1. Find first $9B (ATASCII EOL) — determines effective length
    uint8_t effectiveLen = len;
    for (uint8_t i = 0; i < len; ++i) {
        if (record[i] == Sio::ATASCII_EOL) {
            effectiveLen = i;
            break;
        }
    }

    // 2. Strip trailing spaces from [0, effectiveLen)
    while (effectiveLen > 0 && record[effectiveLen - 1] == ' ') {
        --effectiveLen;
    }

    // 3. Scan for ESC ~ sequences (0x1B 0x7E C V) before ATASCII conversion.
    //    ESC (0x1B) maps to space after conversion — must intercept here.
    //    Sequences are only interpreted for TSPL generators; for PCL/ESC/P
    //    the bytes fall through to toAscii() as normal (0x1B → space).
    const bool isTspl = (m_generator->protocol() == ProtocolType::TSPL);
    std::string result;
    result.reserve(effectiveLen);

    uint8_t i = 0;
    while (i < effectiveLen) {
        // Need 4 bytes: ESC(i), ~(i+1), cmd(i+2), val(i+3)
        if (record[i] == 0x1B
                && static_cast<uint8_t>(i + 3) < effectiveLen
                && record[i + 1] == 0x7E) {
            if (isTspl) {
                applyEscCmd(record[i + 2], record[i + 3],
                            m_config, m_defaultConfig, m_configDirty,
                            m_printRequested,
                            m_saveRequested, m_saveSlot,
                            m_loadRequested, m_loadSlot);
            }
            i += 4;
        } else {
            result += AtasciiConverter::toAscii(record[i]);
            ++i;
        }
    }
    return result;
}

void LineAssembler::ingest(const uint8_t* record, uint8_t len) {
    std::string processed = processRecord(record, len);

    if (m_mode == Mode::COL_40) {
        if (!processed.empty()) {
            flushConfigIfDirty();
            m_generator->writeLine(processed);
        } else if (m_generator->protocol() == ProtocolType::ESCPOS) {
            // Blank LPRINT "" → send one LF so the paper advances one line.
            m_generator->writeBlank();
        }
        // TSPL: config-only empty records produce no label (unchanged).
        return;
    }

    // COL_80: join two 40-byte records into one line
    if (!m_halfPending) {
        // First record — check if line already ended within this record
        bool eolFound = false;
        for (uint8_t j = 0; j < len; ++j) {
            if (record[j] == Sio::ATASCII_EOL) { eolFound = true; break; }
        }

        if (eolFound || processed.empty()) {
            // Line ended within first 40 columns OR config-only record
            if (!processed.empty()) {
                flushConfigIfDirty();
                m_generator->writeLine(processed);
            }
            m_halfPending = false;
        } else {
            m_halfLine    = processed;
            m_halfPending = true;
        }
    } else {
        // Second record — join and emit
        std::string full = m_halfLine + processed;
        m_halfLine.clear();
        m_halfPending = false;
        if (!full.empty()) {
            flushConfigIfDirty();
            m_generator->writeLine(full);
        }
    }
}

void LineAssembler::flush() {
    if (m_halfPending) {
        if (!m_halfLine.empty()) {
            flushConfigIfDirty();
            m_generator->writeLine(m_halfLine);
        }
        m_halfLine.clear();
        m_halfPending = false;
    }
}

void LineAssembler::reset() {
    m_halfLine.clear();
    m_halfPending = false;
    // Note: m_config is intentionally NOT reset here.
    // ESC ~ settings persist until ESC ~ D 0 is received.
}
