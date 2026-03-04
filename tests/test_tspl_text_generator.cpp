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
    // default config: 400x240 dots, gap=24, margin=10,10
    for (int i = 1; i <= 5; ++i)
        gen.writeLine("Line" + std::to_string(i));

    auto job = gen.flush();
    REQUIRE_FALSE(job.rawData.empty());

    std::string s = asStr(job.rawData);
    // Dynamic height: marginTop(10) + 5 lines × 24 dots = 130 dots → 130/8 = 16 mm
    // Width: 400/8 = 50 mm. GAP is always 0 for line-printer behaviour.
    REQUIRE(s.find("SIZE 50 mm,16 mm\r\n") != std::string::npos);
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

    // First line at y=marginTopDots=10
    REQUIRE(s.find("TEXT 10,10,\"TSS24.BF2\",0,1,1,\"Hello\"\r\n") != std::string::npos);
    // Second line at y=10+24=34
    REQUIRE(s.find("TEXT 10,34,\"TSS24.BF2\",0,1,1,\"World\"\r\n") != std::string::npos);
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
    // Default: linesPerLabel = (240-10)/24 = 9
    // Write 10 lines → first label has 9, second has 1
    for (int i = 0; i < 10; ++i)
        gen.writeLine("L" + std::to_string(i));

    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    // Should contain two SIZE headers (two labels, each with dynamic height).
    // First label: 9 lines → 10+9×24=226 dots → 28 mm
    // Second label: 1 line → 10+1×24=34 dots → 4 mm
    size_t first = s.find("SIZE 50 mm,28 mm\r\n");
    REQUIRE(first != std::string::npos);
    size_t second = s.find("SIZE 50 mm,4 mm\r\n");
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
    REQUIRE(s.find("TEXT 10,10,\"TSS24.BF2\",0,1,1,\"Before\"\r\n") != std::string::npos);
    REQUIRE(s.find("TEXT 10,34,\"TSS24.BF2\",0,1,1,\"\"\r\n") != std::string::npos);
    REQUIRE(s.find("TEXT 10,58,\"TSS24.BF2\",0,1,1,\"After\"\r\n") != std::string::npos);
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
    // Dynamic height: marginTop(20) + 1 line × 24 × fontYMul(2) = 68 dots → 8 mm
    // GAP is always 0 regardless of cfg.gapDots.
    REQUIRE(s.find("SIZE 100 mm,8 mm\r\n") != std::string::npos);
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
