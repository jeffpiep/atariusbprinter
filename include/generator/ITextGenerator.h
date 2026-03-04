#pragma once
#include "protocol/PrintJob.h"
#include "protocol/ProtocolType.h"
#include "generator/TextConfig.h"
#include <memory>
#include <string_view>

class ITextGenerator {
public:
    virtual ~ITextGenerator() = default;

    // Configure the generator. Must be called before any writeLine().
    virtual void configure(const TextConfig& config) = 0;

    // Append one line of plain ASCII text.
    virtual void writeLine(std::string_view line) = 0;

    // Write a blank line.
    virtual void writeBlank() = 0;

    // Finalise the current page/label and return a PrintJob.
    // Resets internal state so the generator is ready for the next page/label.
    virtual PrintJob flush() = 0;

    // Discard buffered content without producing a job.
    virtual void reset() = 0;

    // Protocol type this generator targets.
    virtual ProtocolType protocol() const = 0;
};

// Factory function: create a generator for the given protocol.
std::unique_ptr<ITextGenerator> makeTextGenerator(ProtocolType proto);
