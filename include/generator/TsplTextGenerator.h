#pragma once
#include "generator/ITextGenerator.h"
#include <vector>
#include <string>

class TsplTextGenerator : public ITextGenerator {
public:
    void configure(const TextConfig& config) override;
    void writeLine(std::string_view line) override;
    void writeBlank() override;
    PrintJob flush() override;
    void reset() override;
    ProtocolType protocol() const override { return ProtocolType::TSPL; }

private:
    void doFlushLabel(std::vector<uint8_t>& out);
    static std::string dotsToMmStr(uint16_t dots);

    TextConfig           m_cfg;
    bool                 m_configured = false;
    std::vector<std::string> m_lines;
    uint16_t             m_currentY   = 0;
    static constexpr uint8_t DOTS_PER_MM       = 8; // 203 dpi
    static constexpr uint16_t LINE_HEIGHT_DOTS = 24; // default for font size 1x
};
