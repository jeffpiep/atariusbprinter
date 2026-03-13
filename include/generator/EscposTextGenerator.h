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

private:
    TextConfig           m_cfg;
    std::vector<uint8_t> m_buf;
    bool                 m_configured    = false;
    bool                 m_lineSpacingSet = false; // true once ESC 2 has been sent
};
