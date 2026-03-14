#pragma once
#include "generator/ITextGenerator.h"
#include "generator/TextConfig.h"
#include "protocol/PrintJob.h"
#include <vector>
#include <string_view>

// ESC/POS thermal receipt printer generator — line-oriented, like the Atari 820/822.
// writeLine() / writeBlank() accumulate lines into an internal buffer; no init
// sequences, no paper feed commands, no auto-cut. The caller decides when to flush
// (e.g., after SIO goes quiet). flush() drains the buffer into a PrintJob.
class EscposTextGenerator : public ITextGenerator {
public:
    void configure(const TextConfig& config) override;
    void writeLine(std::string_view line) override;
    void writeBlank() override;
    PrintJob flush() override;
    void reset() override;
    ProtocolType protocol() const override { return ProtocolType::ESCPOS; }

    // Arm a user-defined character set for download on first use.
    // 'data' must point to the complete ESC & byte sequence (including the
    // 1B 26 y c1 c2 header) and remain valid for the object's lifetime.
    // Pass nullptr to disable (use printer built-in font).
    void setCustomFont(const uint8_t* data, size_t size);

    // Mark the font as already downloaded (e.g. sent eagerly on device attach).
    // Prevents writeLine() from prepending the ESC & sequence again.
    void markFontDownloaded() { m_fontDownloaded = true; }

private:
    TextConfig           m_cfg;
    std::vector<uint8_t> m_buf;
    bool                 m_configured     = false;
    bool                 m_lineSpacingSet  = false; // true once ESC 2 has been sent
    const uint8_t*       m_fontData        = nullptr;
    size_t               m_fontSize        = 0;
    bool                 m_fontDownloaded  = false; // reset() clears so re-download on reconnect
};
