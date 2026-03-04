#pragma once
#include "generator/ITextGenerator.h"
#include <cstdint>
#include <string>

class LineAssembler {
public:
    enum class Mode { COL_40, COL_80 };

    explicit LineAssembler(Mode mode, ITextGenerator& generator);

    // Feed one 40-byte record.
    // COL_40: immediately assembles and calls generator.writeLine().
    // COL_80: buffers first half; second call joins and calls generator.writeLine().
    void ingest(const uint8_t* record, uint8_t len);

    // Force-emit any buffered half-line (COL_80: early EOL in first half).
    void flush();

    // Clear buffer without emitting.
    void reset();

private:
    // Scan record for $9B, strip trailing spaces, convert ATASCII.
    std::string processRecord(const uint8_t* record, uint8_t len);

    Mode            m_mode;
    ITextGenerator& m_generator;
    std::string     m_halfLine;
    bool            m_halfPending = false;
};
