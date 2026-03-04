#include "sio/LineAssembler.h"
#include "sio/AtasciiConverter.h"
#include "sio/SioProtocol.h"

LineAssembler::LineAssembler(Mode mode, ITextGenerator& generator)
    : m_mode(mode), m_generator(generator) {}

std::string LineAssembler::processRecord(const uint8_t* record, uint8_t len) {
    // 1. Find first $9B (ATASCII EOL)
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

    // 3. Convert ATASCII → ASCII
    std::string result;
    result.reserve(effectiveLen);
    for (uint8_t i = 0; i < effectiveLen; ++i) {
        result += AtasciiConverter::toAscii(record[i]);
    }
    return result;
}

void LineAssembler::ingest(const uint8_t* record, uint8_t len) {
    std::string processed = processRecord(record, len);

    if (m_mode == Mode::COL_40) {
        m_generator.writeLine(processed);
        return;
    }

    // COL_80: join two 40-byte records into one line
    if (!m_halfPending) {
        // First record — check if line already ended within this record
        bool eolFound = false;
        for (uint8_t i = 0; i < len; ++i) {
            if (record[i] == Sio::ATASCII_EOL) { eolFound = true; break; }
        }

        if (eolFound) {
            // Line ended within first 40 columns — emit immediately
            m_generator.writeLine(processed);
            m_halfPending = false;
        } else {
            m_halfLine    = processed;
            m_halfPending = true;
        }
    } else {
        // Second record — join and emit
        m_generator.writeLine(m_halfLine + processed);
        m_halfLine.clear();
        m_halfPending = false;
    }
}

void LineAssembler::flush() {
    if (m_halfPending) {
        m_generator.writeLine(m_halfLine);
        m_halfLine.clear();
        m_halfPending = false;
    }
}

void LineAssembler::reset() {
    m_halfLine.clear();
    m_halfPending = false;
}
