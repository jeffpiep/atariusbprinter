#include <catch2/catch_test_macros.hpp>
#include "generator/EscposTextGenerator.h"
#include "generator/ITextGenerator.h"
#include "generator/TextConfig.h"
#include <string>
#include <vector>

static std::string asStr(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

// ── protocol() ────────────────────────────────────────────────────────────────

TEST_CASE("EscposTextGenerator: protocol() returns ESCPOS", "[escpos_gen]") {
    EscposTextGenerator gen;
    REQUIRE(gen.protocol() == ProtocolType::ESCPOS);
}

// ── flush() before writeLine() ────────────────────────────────────────────────

TEST_CASE("EscposTextGenerator: flush before writeLine returns empty rawData", "[escpos_gen]") {
    EscposTextGenerator gen;
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

// ── writeLine() content ───────────────────────────────────────────────────────

TEST_CASE("EscposTextGenerator: writeLine then flush: ESC 2 + text + LF", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("Hello");
    auto job = gen.flush();

    REQUIRE_FALSE(job.rawData.empty());
    // First line: ESC 2 (default 1/6-inch line spacing) + text + LF
    const auto& d = job.rawData;
    REQUIRE(d.size() == 8); // 2 + 5 + 1
    REQUIRE(d[0] == 0x1B);
    REQUIRE(d[1] == '2');
    std::string text(d.begin() + 2, d.end() - 1);
    REQUIRE(text == "Hello");
    REQUIRE(d.back() == '\x0A');
}

TEST_CASE("EscposTextGenerator: second writeLine omits ESC 3 (spacing already set)", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("First");
    gen.flush(); // consume first line (clears buffer but NOT m_lineSpacingSet)
    gen.writeLine("Second");
    auto job = gen.flush();

    // Second line has no ESC 3 prefix — just text + LF
    std::string s = asStr(job.rawData);
    REQUIRE(s == "Second\x0A");
}

TEST_CASE("EscposTextGenerator: no ESC @ init sequence", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("Test");
    auto job = gen.flush();

    // ESC @ (0x1B 0x40) must NOT be present — raw data is text + LF only
    bool foundEscAt = false;
    const auto& d = job.rawData;
    for (size_t i = 0; i + 1 < d.size(); ++i) {
        if (d[i] == 0x1B && d[i+1] == '@') { foundEscAt = true; break; }
    }
    REQUIRE_FALSE(foundEscAt);
}

TEST_CASE("EscposTextGenerator: no GS V cut command", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("Test");
    auto job = gen.flush();

    // GS V (0x1D 0x56) must NOT be present — no auto-cut
    bool foundGsV = false;
    const auto& d = job.rawData;
    for (size_t i = 0; i + 1 < d.size(); ++i) {
        if (d[i] == 0x1D && d[i+1] == 0x56) { foundGsV = true; break; }
    }
    REQUIRE_FALSE(foundGsV);
}

// ── writeBlank() ─────────────────────────────────────────────────────────────

TEST_CASE("EscposTextGenerator: writeBlank then flush produces ESC 2 + LF", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeBlank();
    auto job = gen.flush();

    // First blank: ESC 2 + LF (3 bytes)
    REQUIRE(job.rawData.size() == 3);
    REQUIRE(job.rawData[0] == 0x1B);
    REQUIRE(job.rawData[1] == '2');
    REQUIRE(job.rawData[2] == '\x0A');
}

// ── each writeLine() overwrites the previous ──────────────────────────────────

TEST_CASE("EscposTextGenerator: second writeLine appends to first", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("First");
    gen.writeLine("Second"); // appends; ESC 2 only on the very first line
    auto job = gen.flush();

    std::string s = asStr(job.rawData);
    // Both lines in buffer; ESC 2 prefix only before the first line
    REQUIRE(s == "\x1B\x32""First\x0ASecond\x0A");
}

// ── flush after flush ─────────────────────────────────────────────────────────

TEST_CASE("EscposTextGenerator: flush after flush returns empty", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("Once");
    auto j1 = gen.flush();
    REQUIRE_FALSE(j1.rawData.empty());

    auto j2 = gen.flush();
    REQUIRE(j2.rawData.empty());
}

// ── reset() ──────────────────────────────────────────────────────────────────

TEST_CASE("EscposTextGenerator: reset clears pending buffer", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("Discard me");
    gen.reset();
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

TEST_CASE("EscposTextGenerator: reset re-arms ESC 2 on next writeLine", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("First");
    gen.flush();       // consume — spacing set
    gen.reset();       // clears spacing flag
    gen.writeLine("After reset");
    auto job = gen.flush();
    // ESC 2 should appear again after reset
    const auto& d = job.rawData;
    REQUIRE(d.size() >= 2);
    REQUIRE(d[0] == 0x1B);
    REQUIRE(d[1] == '2');
}

// ── jobId propagation ─────────────────────────────────────────────────────────

TEST_CASE("EscposTextGenerator: jobId from TextConfig propagates to PrintJob", "[escpos_gen]") {
    EscposTextGenerator gen;
    TextConfig cfg;
    cfg.jobId = "myreceipt";
    gen.configure(cfg);
    gen.writeLine("Hi");
    auto job = gen.flush();
    REQUIRE(job.jobId == "myreceipt");
}

TEST_CASE("EscposTextGenerator: default jobId is 'escpos'", "[escpos_gen]") {
    EscposTextGenerator gen;
    gen.writeLine("Hi");
    auto job = gen.flush();
    REQUIRE(job.jobId == "escpos");
}

// ── factory ──────────────────────────────────────────────────────────────────

TEST_CASE("makeTextGenerator - ESCPOS type", "[gen_factory]") {
    auto g = makeTextGenerator(ProtocolType::ESCPOS);
    REQUIRE(g != nullptr);
    REQUIRE(g->protocol() == ProtocolType::ESCPOS);
}
