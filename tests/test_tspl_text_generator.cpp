#include <catch2/catch_test_macros.hpp>
#include "generator/TsplTextGenerator.h"
#include "generator/TextConfig.h"
#include <string>
#include <vector>
#include <cstring>

// Helper: convert rawData to string for searching
static std::string asStr(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

// ── flush() with default config ────────────────────────────────────────────────

TEST_CASE("TsplTextGenerator: 5-line label - correct header", "[tspl_gen]") {
    TsplTextGenerator gen;
    // default config: 400x240 dots, gap=24, margin=0,0
    for (int i = 1; i <= 5; ++i)
        gen.writeLine("Line" + std::to_string(i));

    auto job = gen.flush();
    REQUIRE_FALSE(job.rawData.empty());

    std::string s = asStr(job.rawData);
    // Dynamic height: marginTop(0) + 5 lines × 24 dots = 120 dots → ceil(120/8) = 15 mm
    // Width: 400/8 = 50 mm. GAP is always 0 for line-printer behaviour.
    REQUIRE(s.find("SIZE 50 mm,15 mm\r\n") != std::string::npos);
    REQUIRE(s.find("GAP 0 mm,0 mm\r\n") != std::string::npos);
    REQUIRE(s.find("DIRECTION 0\r\n") != std::string::npos);
    REQUIRE(s.find("CLS\r\n") != std::string::npos);
    REQUIRE(s.find("PRINT 1,1\r\n") != std::string::npos);
}

TEST_CASE("TsplTextGenerator: 5-line label - TEXT commands", "[tspl_gen]") {
    TsplTextGenerator gen;
    gen.writeLine("Hello");
    gen.writeLine("World");

    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    // First line at y=marginTopDots=0
    REQUIRE(s.find("TEXT 0,0,\"TSS24.BF2\",0,1,1,\"Hello\"\r\n") != std::string::npos);
    // Second line at y=0+24=24
    REQUIRE(s.find("TEXT 0,24,\"TSS24.BF2\",0,1,1,\"World\"\r\n") != std::string::npos);
}

TEST_CASE("TsplTextGenerator: empty flush returns empty rawData", "[tspl_gen]") {
    TsplTextGenerator gen;
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

TEST_CASE("TsplTextGenerator: reset clears buffered lines", "[tspl_gen]") {
    TsplTextGenerator gen;
    gen.writeLine("Line1");
    gen.reset();
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

TEST_CASE("TsplTextGenerator: auto page-break splits into two labels", "[tspl_gen]") {
    TsplTextGenerator gen;
    // Default: linesPerLabel = (240-0)/24 = 10
    // Write 11 lines → first label has 10, second has 1
    for (int i = 0; i < 11; ++i)
        gen.writeLine("L" + std::to_string(i));

    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    // Should contain two SIZE headers (two labels, each with dynamic height).
    // First label: 10 lines → 0+10×24=240 dots → ceil(240/8)=30 mm
    // Second label: 1 line → 0+1×24=24 dots → ceil(24/8)=3 mm
    size_t first = s.find("SIZE 50 mm,30 mm\r\n");
    REQUIRE(first != std::string::npos);
    size_t second = s.find("SIZE 50 mm,3 mm\r\n");
    REQUIRE(second != std::string::npos);

    // Should contain two PRINT commands
    size_t p1 = s.find("PRINT 1,1\r\n");
    REQUIRE(p1 != std::string::npos);
    size_t p2 = s.find("PRINT 1,1\r\n", p1 + 1);
    REQUIRE(p2 != std::string::npos);
}

TEST_CASE("TsplTextGenerator: protocol() returns TSPL", "[tspl_gen]") {
    TsplTextGenerator gen;
    REQUIRE(gen.protocol() == ProtocolType::TSPL);
}

TEST_CASE("TsplTextGenerator: writeBlank emits empty TEXT command", "[tspl_gen]") {
    TsplTextGenerator gen;
    gen.writeLine("Before");
    gen.writeBlank();
    gen.writeLine("After");

    auto job = gen.flush();
    std::string s = asStr(job.rawData);
    REQUIRE(s.find("TEXT 0,0,\"TSS24.BF2\",0,1,1,\"Before\"\r\n") != std::string::npos);
    REQUIRE(s.find("TEXT 0,24,\"TSS24.BF2\",0,1,1,\"\"\r\n") != std::string::npos);
    REQUIRE(s.find("TEXT 0,48,\"TSS24.BF2\",0,1,1,\"After\"\r\n") != std::string::npos);
}

TEST_CASE("TsplTextGenerator: custom config applied", "[tspl_gen]") {
    TsplTextGenerator gen;
    TextConfig cfg;
    cfg.labelWidthDots  = 800; // 100 mm
    cfg.labelHeightDots = 480; // 60 mm
    cfg.gapDots         = 16;  // 2 mm
    cfg.marginLeftDots  = 20;
    cfg.marginTopDots   = 20;
    cfg.tsplFontXMul    = 2;
    cfg.tsplFontYMul    = 2;
    gen.configure(cfg);
    gen.writeLine("Test");

    auto job = gen.flush();
    std::string s = asStr(job.rawData);
    // Dynamic height: marginTop(20) + 1 line × 24 × fontYMul(2) = 68 dots → ceil(68/8)=9 mm
    // GAP is always 0 regardless of cfg.gapDots.
    REQUIRE(s.find("SIZE 100 mm,9 mm\r\n") != std::string::npos);
    REQUIRE(s.find("GAP 0 mm,0 mm\r\n") != std::string::npos);
    REQUIRE(s.find("TEXT 20,20,\"TSS24.BF2\",0,2,2,\"Test\"\r\n") != std::string::npos);
}

TEST_CASE("TsplTextGenerator: flush after flush returns empty", "[tspl_gen]") {
    TsplTextGenerator gen;
    gen.writeLine("Once");
    auto job1 = gen.flush();
    REQUIRE_FALSE(job1.rawData.empty());

    // Second flush without new lines → empty
    auto job2 = gen.flush();
    REQUIRE(job2.rawData.empty());
}
