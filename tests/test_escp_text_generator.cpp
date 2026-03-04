#include <catch2/catch_test_macros.hpp>
#include "generator/EscpTextGenerator.h"
#include "generator/TextConfig.h"
#include <string>
#include <vector>

static std::string asStr(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

// ── Init sequence verification ────────────────────────────────────────────────

TEST_CASE("EscpTextGenerator: init sequence - NLQ mode (default)", "[escp_gen]") {
    EscpTextGenerator gen;
    gen.writeLine("Hello");
    auto job = gen.flush();
    REQUIRE_FALSE(job.rawData.empty());

    const auto& d = job.rawData;
    // ESC @ at position 0
    REQUIRE(d.size() >= 2);
    REQUIRE(d[0] == 0x1B);
    REQUIRE(d[1] == '@');
    // ESC x 1 (NLQ mode)
    REQUIRE(d[2] == 0x1B);
    REQUIRE(d[3] == 'x');
    REQUIRE(d[4] == 0x01);
    // ESC P (10 CPI)
    REQUIRE(d[5] == 0x1B);
    REQUIRE(d[6] == 'P');
    // ESC 3 n  where n=216/6=36=0x24
    REQUIRE(d[7] == 0x1B);
    REQUIRE(d[8] == '3');
    REQUIRE(d[9] == 0x24);
}

TEST_CASE("EscpTextGenerator: init sequence - condensed mode", "[escp_gen]") {
    EscpTextGenerator gen;
    TextConfig cfg;
    cfg.escpCondensed = true;
    gen.configure(cfg);
    gen.writeLine("X");
    auto job = gen.flush();
    const auto& d = job.rawData;

    REQUIRE(d[0] == 0x1B);
    REQUIRE(d[1] == '@');
    // Condensed: SI (0x0F) — no ESC x 1 or ESC P
    REQUIRE(d[2] == 0x0F);
    // ESC 3 n
    REQUIRE(d[3] == 0x1B);
    REQUIRE(d[4] == '3');
    REQUIRE(d[5] == 0x24); // 216/6=36
}

TEST_CASE("EscpTextGenerator: line spacing for 8 LPI", "[escp_gen]") {
    EscpTextGenerator gen;
    TextConfig cfg;
    cfg.escpLpi = 8;
    gen.configure(cfg);
    gen.writeLine("X");
    auto job = gen.flush();
    const auto& d = job.rawData;

    // Find ESC 3 byte
    bool found = false;
    for (size_t i = 0; i + 2 < d.size(); ++i) {
        if (d[i] == 0x1B && d[i+1] == '3') {
            REQUIRE(d[i+2] == 27); // 216/8=27
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

// ── Body content ──────────────────────────────────────────────────────────────

TEST_CASE("EscpTextGenerator: 10-line job byte content", "[escp_gen]") {
    EscpTextGenerator gen;
    for (int i = 0; i < 10; ++i)
        gen.writeLine("Line" + std::to_string(i));

    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    for (int i = 0; i < 10; ++i)
        REQUIRE(s.find("Line" + std::to_string(i) + "\r\n") != std::string::npos);
}

TEST_CASE("EscpTextGenerator: flush appends form feed by default", "[escp_gen]") {
    EscpTextGenerator gen;
    gen.writeLine("Data");
    auto job = gen.flush();

    REQUIRE_FALSE(job.rawData.empty());
    REQUIRE(job.rawData.back() == '\x0c');
}

TEST_CASE("EscpTextGenerator: suppressFormFeed omits form feed", "[escp_gen]") {
    EscpTextGenerator gen;
    TextConfig cfg;
    cfg.suppressFormFeed = true;
    gen.configure(cfg);
    gen.writeLine("Receipt line");
    auto job = gen.flush();

    REQUIRE_FALSE(job.rawData.empty());
    REQUIRE(job.rawData.back() != '\x0c');
    // Last bytes should be \r\n
    size_t n = job.rawData.size();
    REQUIRE(job.rawData[n-2] == '\r');
    REQUIRE(job.rawData[n-1] == '\n');
}

TEST_CASE("EscpTextGenerator: empty flush returns empty rawData", "[escp_gen]") {
    EscpTextGenerator gen;
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

TEST_CASE("EscpTextGenerator: reset clears buffer", "[escp_gen]") {
    EscpTextGenerator gen;
    gen.writeLine("Should be discarded");
    gen.reset();
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

TEST_CASE("EscpTextGenerator: init sequence emitted only once", "[escp_gen]") {
    EscpTextGenerator gen;
    gen.writeLine("Line1");
    gen.writeLine("Line2");
    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    // ESC @ should appear exactly once
    size_t count = 0;
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if ((uint8_t)s[i] == 0x1B && s[i+1] == '@') ++count;
    }
    REQUIRE(count == 1);
}

TEST_CASE("EscpTextGenerator: protocol() returns ESCP", "[escp_gen]") {
    EscpTextGenerator gen;
    REQUIRE(gen.protocol() == ProtocolType::ESCP);
}

TEST_CASE("EscpTextGenerator: writeBlank emits CRLF", "[escp_gen]") {
    EscpTextGenerator gen;
    gen.writeLine("A");
    gen.writeBlank();
    gen.writeLine("B");
    auto job = gen.flush();
    std::string s = asStr(job.rawData);
    REQUIRE(s.find("A\r\n\r\nB\r\n") != std::string::npos);
}

TEST_CASE("EscpTextGenerator: flush after flush returns empty", "[escp_gen]") {
    EscpTextGenerator gen;
    gen.writeLine("Once");
    auto j1 = gen.flush();
    REQUIRE_FALSE(j1.rawData.empty());

    auto j2 = gen.flush();
    REQUIRE(j2.rawData.empty());
}

// ── Factory function ──────────────────────────────────────────────────────────

TEST_CASE("makeTextGenerator - TSPL type", "[gen_factory]") {
    auto g = makeTextGenerator(ProtocolType::TSPL);
    REQUIRE(g != nullptr);
    REQUIRE(g->protocol() == ProtocolType::TSPL);
}

TEST_CASE("makeTextGenerator - PCL type", "[gen_factory]") {
    auto g = makeTextGenerator(ProtocolType::PCL);
    REQUIRE(g != nullptr);
    REQUIRE(g->protocol() == ProtocolType::PCL);
}

TEST_CASE("makeTextGenerator - ESCP type", "[gen_factory]") {
    auto g = makeTextGenerator(ProtocolType::ESCP);
    REQUIRE(g != nullptr);
    REQUIRE(g->protocol() == ProtocolType::ESCP);
}
