#pragma once
#include "generator/ITextGenerator.h"
#include "generator/TextConfig.h"
#include <cstdint>
#include <string>

class LineAssembler {
public:
    enum class Mode { COL_40, COL_80 };

    // defaultConfig: baseline used by ESC ~ R 0 to reset settings.
    // Omitting it uses TextConfig{} defaults — existing call sites unchanged.
    explicit LineAssembler(Mode mode,
                           ITextGenerator& generator,
                           const TextConfig& defaultConfig = TextConfig{});

    // Feed one 40-byte record.
    // COL_40: immediately assembles and calls generator.writeLine().
    // COL_80: buffers first half; second call joins and calls generator.writeLine().
    // Records containing only ESC ~ sequences produce no writeLine() call.
    void ingest(const uint8_t* record, uint8_t len);

    // Force-emit any buffered half-line (COL_80: early EOL in first half).
    void flush();

    // Clear buffer without emitting. Does NOT reset ESC ~ config state.
    void reset();

    // Replace the generator used for output. Call when the protocol changes
    // at runtime (e.g. after USB printer auto-detection).
    void setGenerator(ITextGenerator& gen);

    // Returns true (once) if an ESC ~ P sequence was received since last call.
    bool takePrintRequest();

    // Returns true (once) if ESC ~ S was received; sets slot (0–9).
    bool takeSaveRequest(uint8_t& slot);

    // Returns true (once) if ESC ~ R was received; sets slot (0–9).
    bool takeLoadRequest(uint8_t& slot);

    // Apply an externally-loaded TextConfig (e.g. from flash) and mark dirty.
    void setConfig(const TextConfig& cfg);

    // Read current accumulated config (e.g. to pass to FlashConfig::save).
    const TextConfig& getConfig() const;

private:
    // Scan record for $9B, strip trailing spaces, parse ESC ~ sequences,
    // convert remaining ATASCII bytes to ASCII.
    std::string processRecord(const uint8_t* record, uint8_t len);

    // Call generator.configure(m_config) if m_configDirty and protocol==TSPL.
    void flushConfigIfDirty();

    Mode            m_mode;
    ITextGenerator* m_generator;
    std::string     m_halfLine;
    bool            m_halfPending = false;

    TextConfig      m_defaultConfig;        // baseline for ESC ~ R 0
    TextConfig      m_config;               // current accumulated config
    bool            m_configDirty    = false;
    bool            m_printRequested = false;  // set by ESC ~ P
    bool            m_saveRequested  = false;  // set by ESC ~ S
    uint8_t         m_saveSlot       = 0;
    bool            m_loadRequested  = false;  // set by ESC ~ R
    uint8_t         m_loadSlot       = 0;
};
