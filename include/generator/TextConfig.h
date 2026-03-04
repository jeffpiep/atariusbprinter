#pragma once
#include <cstdint>
#include <string>

struct TextConfig {
    // TSPL: label / page dimensions (dots at 203 dpi = 8 dots/mm)
    uint16_t labelWidthDots  = 400;   // 50 mm
    uint16_t labelHeightDots = 240;   // 30 mm
    uint8_t  gapDots         = 24;    // 3 mm

    // TSPL: text placement
    uint16_t marginLeftDots  = 10;
    uint16_t marginTopDots   = 10;
    uint8_t  tsplFontId      = 3;     // built-in font 1–8 (TSS24.BF2 alias)
    uint8_t  tsplFontXMul    = 1;
    uint8_t  tsplFontYMul    = 1;

    // PCL text settings
    uint8_t  pclLinesPerPage = 60;
    uint8_t  pclCpi          = 10;   // Characters per inch (10 or 12)
    uint8_t  pclLpi          = 6;    // Lines per inch (6 or 8)

    // ESC/P settings
    bool     escpCondensed   = false; // true → 17 CPI condensed mode
    uint8_t  escpLpi         = 6;
    bool     suppressFormFeed = false; // suppress \x0c at flush() for receipt printers

    // Common
    std::string jobId;
};
