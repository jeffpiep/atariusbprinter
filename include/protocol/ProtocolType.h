#pragma once

enum class ProtocolType {
    TSPL,
    PCL,
    ESCP,
    UNKNOWN
};

inline const char* protocolTypeName(ProtocolType t) {
    switch (t) {
        case ProtocolType::TSPL:    return "TSPL";
        case ProtocolType::PCL:     return "PCL";
        case ProtocolType::ESCP:    return "ESCP";
        case ProtocolType::UNKNOWN: return "UNKNOWN";
    }
    return "?";
}
