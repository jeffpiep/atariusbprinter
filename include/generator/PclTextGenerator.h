#pragma once
#include "generator/ITextGenerator.h"
#include <vector>

class PclTextGenerator : public ITextGenerator {
public:
    void configure(const TextConfig& config) override;
    void writeLine(std::string_view line) override;
    void writeBlank() override;
    PrintJob flush() override;
    void reset() override;
    ProtocolType protocol() const override { return ProtocolType::PCL; }

private:
    TextConfig           m_cfg;
    bool                 m_configured = false;
    std::vector<uint8_t> m_body;      // accumulated line data
    int                  m_lineCount  = 0;
};
