#include <catch2/catch_test_macros.hpp>
#include "generator/PclTextGenerator.h"
#include "generator/TextConfig.h"
#include <string>
#include <algorithm>

static std::string asStr(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

// ── Basic output format ────────────────────────────────────────────────────────

TEST_CASE("PclTextGenerator: PCL header present after flush", "[pcl_gen]") {
    PclTextGenerator gen;
    gen.writeLine("Hello");
    auto job = gen.flush();
    REQUIRE_FALSE(job.rawData.empty());

    std::string s = asStr(job.rawData);
    // PCL reset (ESC E = 0x1B 0x45)
    REQUIRE(s.find("\x1b\x45") == 0);
    // Portrait
    REQUIRE(s.find("\x1b&l0O") != std::string::npos);
    // 6 LPI (default)
    REQUIRE(s.find("\x1b&l6D") != std::string::npos);
    // PC-8 symbol set
    REQUIRE(s.find("\x1b(10U") != std::string::npos);
    // 10 CPI (default)
    REQUIRE(s.find("\x1b(s0p10h12v0s0b3T") != std::string::npos);
}

TEST_CASE("PclTextGenerator: PCL footer present after flush", "[pcl_gen]") {
    PclTextGenerator gen;
    gen.writeLine("Hello");
    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    // Footer: \x0c\x1bE at the end
    REQUIRE(s.size() >= 3);
    REQUIRE(s[s.size() - 3] == '\x0c');
    REQUIRE(s[s.size() - 2] == '\x1b');
    REQUIRE(s[s.size() - 1] == 'E');
}

TEST_CASE("PclTextGenerator: body contains line text with CRLF", "[pcl_gen]") {
    PclTextGenerator gen;
    gen.writeLine("Line One");
    gen.writeLine("Line Two");
    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    REQUIRE(s.find("Line One\r\n") != std::string::npos);
    REQUIRE(s.find("Line Two\r\n") != std::string::npos);
}

TEST_CASE("PclTextGenerator: empty flush returns empty rawData", "[pcl_gen]") {
    PclTextGenerator gen;
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

TEST_CASE("PclTextGenerator: reset clears buffer", "[pcl_gen]") {
    PclTextGenerator gen;
    gen.writeLine("Should be discarded");
    gen.reset();
    auto job = gen.flush();
    REQUIRE(job.rawData.empty());
}

// ── Page break at 60 lines ────────────────────────────────────────────────────

TEST_CASE("PclTextGenerator: 70 lines → form feed after line 60", "[pcl_gen]") {
    PclTextGenerator gen;
    for (int i = 0; i < 70; ++i)
        gen.writeLine("L" + std::to_string(i));

    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    // The body should contain exactly one \x0c from the mid-job page break
    // (the footer also has \x0c, so total is 2)
    // Count all FF bytes
    size_t ffCount = 0;
    for (char c : s) if (c == '\x0c') ++ffCount;
    REQUIRE(ffCount == 2); // 1 mid-body + 1 footer

    // L59 and L60 should be in the data with a FF between them
    size_t l59pos = s.find("L59\r\n");
    size_t ffpos  = s.find('\x0c', l59pos);
    size_t l60pos = s.find("L60\r\n");
    REQUIRE(l59pos != std::string::npos);
    REQUIRE(l60pos != std::string::npos);
    // FF appears between L59 and L60
    REQUIRE(ffpos > l59pos);
    REQUIRE(l60pos > ffpos);
}

TEST_CASE("PclTextGenerator: exactly 60 lines → no mid-body FF", "[pcl_gen]") {
    PclTextGenerator gen;
    for (int i = 0; i < 60; ++i)
        gen.writeLine("L");

    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    // Only one FF: the footer
    size_t ffCount = 0;
    for (char c : s) if (c == '\x0c') ++ffCount;
    REQUIRE(ffCount == 1);
}

// ── Condensed / custom CPI/LPI ─────────────────────────────────────────────────

TEST_CASE("PclTextGenerator: custom CPI and LPI in header", "[pcl_gen]") {
    PclTextGenerator gen;
    TextConfig cfg;
    cfg.pclCpi = 12;
    cfg.pclLpi = 8;
    gen.configure(cfg);
    gen.writeLine("Test");
    auto job = gen.flush();
    std::string s = asStr(job.rawData);

    REQUIRE(s.find("\x1b&l8D") != std::string::npos); // 8 LPI
    REQUIRE(s.find("\x1b(s0p12h12v0s0b3T") != std::string::npos); // 12 CPI
}

TEST_CASE("PclTextGenerator: protocol() returns PCL", "[pcl_gen]") {
    PclTextGenerator gen;
    REQUIRE(gen.protocol() == ProtocolType::PCL);
}

TEST_CASE("PclTextGenerator: writeBlank emits empty line", "[pcl_gen]") {
    PclTextGenerator gen;
    gen.writeLine("Before");
    gen.writeBlank();
    gen.writeLine("After");
    auto job = gen.flush();
    std::string s = asStr(job.rawData);
    REQUIRE(s.find("Before\r\n\r\nAfter\r\n") != std::string::npos);
}

TEST_CASE("PclTextGenerator: flush after flush returns empty", "[pcl_gen]") {
    PclTextGenerator gen;
    gen.writeLine("Data");
    auto job1 = gen.flush();
    REQUIRE_FALSE(job1.rawData.empty());

    auto job2 = gen.flush();
    REQUIRE(job2.rawData.empty());
}
