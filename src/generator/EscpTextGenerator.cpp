#include "generator/EscpTextGenerator.h"

void EscpTextGenerator::configure(const TextConfig& config) {
    m_cfg        = config;
    m_configured = true;
    reset();
}

void EscpTextGenerator::reset() {
    m_buf.clear();
    m_initEmitted = false;
}

void EscpTextGenerator::emitInit() {
    if (m_initEmitted) return;

    // ESC/P initialize
    m_buf.push_back(0x1B); m_buf.push_back('@');

    if (!m_cfg.escpCondensed) {
        // NLQ mode
        m_buf.push_back(0x1B); m_buf.push_back('x'); m_buf.push_back(1);
        // 10 CPI normal
        m_buf.push_back(0x1B); m_buf.push_back('P');
    } else {
        // 17 CPI condensed
        m_buf.push_back(0x0F);
    }

    // Set line spacing: ESC 3 n  where n = 216 / lpi
    uint8_t lpiN = (m_cfg.escpLpi > 0) ? static_cast<uint8_t>(216 / m_cfg.escpLpi) : 36;
    m_buf.push_back(0x1B); m_buf.push_back('3'); m_buf.push_back(lpiN);

    m_initEmitted = true;
}

void EscpTextGenerator::writeLine(std::string_view line) {
    if (!m_configured) { TextConfig def; configure(def); }
    emitInit();
    m_buf.insert(m_buf.end(), line.begin(), line.end());
    m_buf.push_back('\r');
    m_buf.push_back('\n');
}

void EscpTextGenerator::writeBlank() {
    writeLine("");
}

PrintJob EscpTextGenerator::flush() {
    PrintJob job;
    job.jobId     = m_cfg.jobId.empty() ? "escp" : m_cfg.jobId;
    job.timeoutMs = 5000;

    if (!m_initEmitted && m_buf.empty()) {
        reset();
        return job;
    }

    if (!m_initEmitted) emitInit();

    if (!m_cfg.suppressFormFeed) {
        m_buf.push_back('\x0c'); // form feed / page eject
    }

    job.rawData = std::move(m_buf);
    reset();
    return job;
}
