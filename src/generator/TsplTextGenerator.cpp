#include "generator/TsplTextGenerator.h"
#include "util/Logger.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "TsplTextGenerator";

// dots → "N mm" string (8 dots per mm at 203 dpi)
std::string TsplTextGenerator::dotsToMmStr(uint16_t dots) {
    // Use integer arithmetic: dots / 8 = mm (truncated)
    uint16_t mm = dots / DOTS_PER_MM;
    char buf[16];
    snprintf(buf, sizeof(buf), "%u mm", mm);
    return buf;
}

void TsplTextGenerator::configure(const TextConfig& config) {
    m_cfg        = config;
    m_configured = true;
    reset();
}

void TsplTextGenerator::reset() {
    m_lines.clear();
    m_currentY = m_configured ? m_cfg.marginTopDots : 10;
}

void TsplTextGenerator::writeLine(std::string_view line) {
    if (!m_configured) {
        TextConfig def;
        configure(def);
    }

    // Auto page-break: if adding this line would overflow the label, flush first
    uint16_t lineH = LINE_HEIGHT_DOTS * m_cfg.tsplFontYMul;
    if (!m_lines.empty() &&
        (m_currentY + lineH) > m_cfg.labelHeightDots) {
        LOG_DEBUG(TAG, "Auto page-break at Y=%u", m_currentY);
        // We can't call flush() here (it returns a job) so we just note:
        // caller should flush manually. Per spec, auto-flush happens in flush().
        // We reset currentY so the line lands at top of next logical page.
        // The actual emission of the prior lines happens in flush().
        // For now, continue accumulating — flush() will handle page breaks.
    }

    m_lines.emplace_back(line.data(), line.size());
    m_currentY += lineH;
}

void TsplTextGenerator::writeBlank() {
    writeLine("");
}

void TsplTextGenerator::doFlushLabel(std::vector<uint8_t>& out) {
    // Emit TSPL label for current m_lines
    auto append = [&](const std::string& s) {
        out.insert(out.end(), s.begin(), s.end());
    };
    auto appendLit = [&](const char* s) {
        while (*s) out.push_back(static_cast<uint8_t>(*s++));
    };

    // Compute label height from actual content so the paper only advances by the
    // text area — no blank gap.  GAP 0,0 means the printer treats consecutive
    // PRINT commands as a continuous roll, giving line-printer behaviour.
    uint16_t lineH = LINE_HEIGHT_DOTS * m_cfg.tsplFontYMul;
    uint16_t contentH = m_cfg.marginTopDots +
                        static_cast<uint16_t>(m_lines.size()) * lineH;
    if (contentH < lineH) contentH = lineH; // never degenerate

    // Header
    append("SIZE " + dotsToMmStr(m_cfg.labelWidthDots) +
           "," + dotsToMmStr(contentH) + "\r\n");
    appendLit("GAP 0 mm,0 mm\r\n");
    appendLit("DIRECTION 0\r\n");
    appendLit("CLS\r\n");

    // TEXT commands
    uint16_t y = m_cfg.marginTopDots;
    for (const auto& line : m_lines) {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "TEXT %u,%u,\"TSS24.BF2\",0,%u,%u,\"%s\"\r\n",
                 m_cfg.marginLeftDots, y,
                 m_cfg.tsplFontXMul, m_cfg.tsplFontYMul,
                 line.c_str());
        appendLit(buf);
        y += lineH;
    }

    appendLit("PRINT 1,1\r\n");
}

PrintJob TsplTextGenerator::flush() {
    PrintJob job;
    job.jobId     = m_cfg.jobId.empty() ? "tspl" : m_cfg.jobId;
    job.timeoutMs = 5000;

    if (m_lines.empty()) {
        reset();
        return job; // empty rawData
    }

    // Handle auto page-break: split lines into pages that fit labelHeightDots
    uint16_t lineH = LINE_HEIGHT_DOTS * m_cfg.tsplFontYMul;
    uint16_t linesPerLabel = (m_cfg.labelHeightDots - m_cfg.marginTopDots) / lineH;
    if (linesPerLabel == 0) linesPerLabel = 1;

    size_t i = 0;
    while (i < m_lines.size()) {
        std::vector<std::string> page;
        uint16_t y = m_cfg.marginTopDots;
        while (i < m_lines.size() && (y + lineH) <= m_cfg.labelHeightDots) {
            page.push_back(m_lines[i++]);
            y += lineH;
        }
        // Temporarily replace m_lines with this page to reuse doFlushLabel
        auto saved = m_lines;
        m_lines = page;
        doFlushLabel(job.rawData);
        m_lines = saved;
    }

    reset();
    return job;
}
