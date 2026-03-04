#pragma once
#include "generator/ITextGenerator.h"
#include <vector>

class EscpTextGenerator : public ITextGenerator {
public:
    void configure(const TextConfig& config) override;
    void writeLine(std::string_view line) override;
    void writeBlank() override;
    PrintJob flush() override;
    void reset() override;
    ProtocolType protocol() const override { return ProtocolType::ESCP; }

private:
    void emitInit();

    TextConfig           m_cfg;
    bool                 m_configured = false;
    bool                 m_initEmitted = false;
    std::vector<uint8_t> m_buf;
};
