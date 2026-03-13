#include "generator/EscposTextGenerator.h"

void EscposTextGenerator::configure(const TextConfig& config) {
    m_cfg        = config;
    m_configured = true;
    reset();
}

void EscposTextGenerator::reset() {
    m_buf.clear();
    m_lineSpacingSet = false;
}

void EscposTextGenerator::writeLine(std::string_view line) {
    if (!m_configured) { TextConfig def; configure(def); }

    // Set line spacing once per session so consecutive LPRINT lines print
    // without extra gap. ESC 2 = default 1/6-inch line spacing (6 LPI).
    if (!m_lineSpacingSet) {
        m_buf.push_back(0x1B); m_buf.push_back('2');
        m_lineSpacingSet = true;
    }

    m_buf.insert(m_buf.end(), line.begin(), line.end());
    m_buf.push_back('\n'); // LF (0x0A)
}

void EscposTextGenerator::writeBlank() {
    if (!m_configured) { TextConfig def; configure(def); }
    if (!m_lineSpacingSet) {
        m_buf.push_back(0x1B); m_buf.push_back('2');
        m_lineSpacingSet = true;
    }
    m_buf.push_back('\n'); // blank line — just a LF
}

PrintJob EscposTextGenerator::flush() {
    PrintJob job;
    job.jobId     = m_cfg.jobId.empty() ? "escpos" : m_cfg.jobId;
    job.timeoutMs = 5000;

    if (m_buf.empty()) {
        return job; // nothing pending
    }

    job.rawData = std::move(m_buf);
    m_buf.clear(); // drain buffer only — preserve m_lineSpacingSet across flushes
    return job;
}
