#include "generator/ITextGenerator.h"
#include "generator/TsplTextGenerator.h"
#include "generator/PclTextGenerator.h"
#include "generator/EscpTextGenerator.h"
#include "generator/EscposTextGenerator.h"
#include <memory>

std::unique_ptr<ITextGenerator> makeTextGenerator(ProtocolType proto) {
    switch (proto) {
        case ProtocolType::TSPL:   return std::make_unique<TsplTextGenerator>();
        case ProtocolType::PCL:    return std::make_unique<PclTextGenerator>();
        case ProtocolType::ESCP:   return std::make_unique<EscpTextGenerator>();
        case ProtocolType::ESCPOS: return std::make_unique<EscposTextGenerator>();
        default:                   return std::make_unique<EscpTextGenerator>();
    }
}
