#include <catch2/catch_test_macros.hpp>
#include "sio/LineAssembler.h"
#include "sio/SioProtocol.h"
#include "generator/EscpTextGenerator.h"
#include <vector>
#include <string>

// Simple capturing generator for testing
class CapturingGenerator : public ITextGenerator {
public:
    std::vector<std::string> lines;

    void configure(const TextConfig&) override {}
    void writeLine(std::string_view s) override { lines.emplace_back(s); }
    void writeBlank() override { lines.emplace_back(""); }
    PrintJob flush() override { return PrintJob{}; }
    void reset() override { lines.clear(); }
    ProtocolType protocol() const override { return ProtocolType::ESCP; }
};

// Build a 40-byte record with content followed by $9B EOL and padding
static std::vector<uint8_t> makeRecord(const std::string& text, bool addEol = true) {
    std::vector<uint8_t> rec(Sio::RECORD_SIZE, 0x20); // space padding
    for (size_t i = 0; i < text.size() && i < Sio::RECORD_SIZE; ++i)
        rec[i] = static_cast<uint8_t>(text[i]);
    if (addEol && text.size() < Sio::RECORD_SIZE)
        rec[text.size()] = Sio::ATASCII_EOL;
    return rec;
}

// ── COL_40 mode ───────────────────────────────────────────────────────────────

TEST_CASE("LineAssembler COL_40: simple text with EOL", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm40(LineAssembler::Mode::COL_40, gen);

    auto rec = makeRecord("Hello");
    asm40.ingest(rec.data(), static_cast<uint8_t>(rec.size()));

    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0] == "Hello");
}

TEST_CASE("LineAssembler COL_40: trailing spaces stripped", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm40(LineAssembler::Mode::COL_40, gen);

    // "Hi  " + EOL + spaces
    auto rec = makeRecord("Hi");  // EOL at pos 2, rest is spaces
    asm40.ingest(rec.data(), static_cast<uint8_t>(rec.size()));

    REQUIRE(gen.lines[0] == "Hi");
}

TEST_CASE("LineAssembler COL_40: $9B mid-record truncates line", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm40(LineAssembler::Mode::COL_40, gen);

    // "ABC$9BXYZ" — should give "ABC"
    std::vector<uint8_t> rec(Sio::RECORD_SIZE, 0x20);
    rec[0] = 'A'; rec[1] = 'B'; rec[2] = 'C';
    rec[3] = Sio::ATASCII_EOL;
    rec[4] = 'X'; rec[5] = 'Y'; rec[6] = 'Z';

    asm40.ingest(rec.data(), static_cast<uint8_t>(rec.size()));
    REQUIRE(gen.lines[0] == "ABC");
}

TEST_CASE("LineAssembler COL_40: full 40-byte record no EOL", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm40(LineAssembler::Mode::COL_40, gen);

    // All 40 bytes of printable characters, no $9B
    std::vector<uint8_t> rec(Sio::RECORD_SIZE, 'A');
    asm40.ingest(rec.data(), static_cast<uint8_t>(rec.size()));

    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0].size() == 40);
    REQUIRE(gen.lines[0] == std::string(40, 'A'));
}

TEST_CASE("LineAssembler COL_40: multiple records", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm40(LineAssembler::Mode::COL_40, gen);

    asm40.ingest(makeRecord("First").data(),  Sio::RECORD_SIZE);
    asm40.ingest(makeRecord("Second").data(), Sio::RECORD_SIZE);
    asm40.ingest(makeRecord("Third").data(),  Sio::RECORD_SIZE);

    REQUIRE(gen.lines.size() == 3);
    REQUIRE(gen.lines[0] == "First");
    REQUIRE(gen.lines[1] == "Second");
    REQUIRE(gen.lines[2] == "Third");
}

TEST_CASE("LineAssembler COL_40: reset clears without emitting", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm40(LineAssembler::Mode::COL_40, gen);

    // In COL_40, reset is essentially a no-op for buffering, but should not emit
    asm40.reset();
    REQUIRE(gen.lines.empty());
}

// ── COL_80 mode ───────────────────────────────────────────────────────────────

TEST_CASE("LineAssembler COL_80: two records joined", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm80(LineAssembler::Mode::COL_80, gen);

    // First record: "HELLO" + spaces (no EOL = full 40 bytes of content)
    std::vector<uint8_t> r1(Sio::RECORD_SIZE, ' ');
    r1[0]='H'; r1[1]='E'; r1[2]='L'; r1[3]='L'; r1[4]='O';

    // Second record: "WORLD" + EOL
    auto r2 = makeRecord("WORLD");

    asm80.ingest(r1.data(), Sio::RECORD_SIZE);
    REQUIRE(gen.lines.empty()); // not emitted yet

    asm80.ingest(r2.data(), Sio::RECORD_SIZE);
    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0] == "HELLOWORLD");
}

TEST_CASE("LineAssembler COL_80: early EOL in first record emits immediately", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm80(LineAssembler::Mode::COL_80, gen);

    // First record has EOL at position 3 → short line, emit immediately
    auto r1 = makeRecord("Hi");  // EOL at pos 2
    asm80.ingest(r1.data(), Sio::RECORD_SIZE);

    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0] == "Hi");
}

TEST_CASE("LineAssembler COL_80: flush emits pending half-line", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm80(LineAssembler::Mode::COL_80, gen);

    // First record without EOL (full line)
    std::vector<uint8_t> r1(Sio::RECORD_SIZE, 'X');
    asm80.ingest(r1.data(), Sio::RECORD_SIZE);
    REQUIRE(gen.lines.empty()); // buffered

    asm80.flush();
    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0].size() == 40);
}

TEST_CASE("LineAssembler COL_80: reset discards pending half-line", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm80(LineAssembler::Mode::COL_80, gen);

    std::vector<uint8_t> r1(Sio::RECORD_SIZE, 'Y');
    asm80.ingest(r1.data(), Sio::RECORD_SIZE);
    REQUIRE(gen.lines.empty());

    asm80.reset(); // should NOT emit
    REQUIRE(gen.lines.empty());

    // After reset, next two records should join normally.
    // r2: all 'A' bytes, no EOL → buffers as half-line.
    // r3: "B" + EOL → joins with r2 and emits.
    std::vector<uint8_t> r2(Sio::RECORD_SIZE, 'A'); // no EOL — full 40-char half-line
    auto r3 = makeRecord("B");
    asm80.ingest(r2.data(), Sio::RECORD_SIZE);
    REQUIRE(gen.lines.empty()); // still buffered
    asm80.ingest(r3.data(), Sio::RECORD_SIZE);
    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0] == std::string(40, 'A') + "B");
}

TEST_CASE("LineAssembler COL_80: trailing spaces stripped from joined line", "[lineassembler]") {
    CapturingGenerator gen;
    LineAssembler asm80(LineAssembler::Mode::COL_80, gen);

    // First: "AB" + 38 spaces (no EOL)
    std::vector<uint8_t> r1(Sio::RECORD_SIZE, ' ');
    r1[0] = 'A'; r1[1] = 'B';

    // Second: "CD" + EOL + spaces
    auto r2 = makeRecord("CD");

    asm80.ingest(r1.data(), Sio::RECORD_SIZE);
    asm80.ingest(r2.data(), Sio::RECORD_SIZE);

    // "AB" (stripped) + "CD" (stripped) = "ABCD"
    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0] == "ABCD");
}
