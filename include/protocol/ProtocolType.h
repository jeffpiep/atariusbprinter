#pragma once

enum class ProtocolType {
    TSPL,
    PCL,
    ESCPOS,
    UNKNOWN
};

inline const char* protocolTypeName(ProtocolType t) {
    switch (t) {
        case ProtocolType::TSPL:    return "TSPL";
        case ProtocolType::PCL:     return "PCL";
        case ProtocolType::ESCPOS:  return "ESCPOS";
        case ProtocolType::UNKNOWN: return "UNKNOWN";
    }
    return "?";
}
