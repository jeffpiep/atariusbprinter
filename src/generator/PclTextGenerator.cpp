#include "generator/PclTextGenerator.h"
#include <cstdio>

void PclTextGenerator::configure(const TextConfig& config) {
    m_cfg        = config;
    m_configured = true;
    reset();
}

void PclTextGenerator::reset() {
    m_body.clear();
    m_lineCount = 0;
}

void PclTextGenerator::writeLine(std::string_view line) {
    if (!m_configured) { TextConfig def; configure(def); }

    // Insert form feed when we hit linesPerPage
    if (m_lineCount > 0 && m_lineCount % m_cfg.pclLinesPerPage == 0) {
        m_body.push_back('\x0c');
    }

    m_body.insert(m_body.end(), line.begin(), line.end());
    m_body.push_back('\r');
    m_body.push_back('\n');
    ++m_lineCount;
}

void PclTextGenerator::writeBlank() {
    writeLine("");
}

PrintJob PclTextGenerator::flush() {
    PrintJob job;
    job.jobId     = m_cfg.jobId.empty() ? "pcl" : m_cfg.jobId;
    job.timeoutMs = 5000;

    if (m_lineCount == 0) {
        reset();
        return job;
    }

    // PCL header
    char header[128];
    int hlen = snprintf(header, sizeof(header),
        "\x1b\x45"                   // PCL reset (ESC E)
        "\x1b&l0O"                   // portrait orientation
        "\x1b&l%dD"                  // lines per inch
        "\x1b(10U"                   // PC-8 symbol set
        "\x1b(s0p%dh12v0s0b3T",      // Courier, CPI, 12pt, upright, medium
        m_cfg.pclLpi, m_cfg.pclCpi);

    // PCL footer
    static const char footer[] = "\x0c\x1b\x45"; // form feed + reset (ESC E)

    job.rawData.reserve(static_cast<size_t>(hlen) + m_body.size() + sizeof(footer));
    job.rawData.insert(job.rawData.end(),
                       reinterpret_cast<uint8_t*>(header),
                       reinterpret_cast<uint8_t*>(header) + hlen);
    job.rawData.insert(job.rawData.end(), m_body.begin(), m_body.end());
    job.rawData.insert(job.rawData.end(),
                       reinterpret_cast<const uint8_t*>(footer),
                       reinterpret_cast<const uint8_t*>(footer) + sizeof(footer) - 1);

    reset();
    return job;
}
