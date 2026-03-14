#include <catch2/catch_test_macros.hpp>
#include "sio/LineAssembler.h"
#include "sio/SioProtocol.h"
#include "generator/TextConfig.h"
#include "protocol/ProtocolType.h"
#include <vector>
#include <string>

// ── Test generator that records configure() and writeLine() calls ──────────

class TrackingGenerator : public ITextGenerator {
public:
    std::vector<TextConfig>  configs;   // one entry per configure() call
    std::vector<std::string> lines;     // one entry per writeLine() call
    ProtocolType             proto = ProtocolType::TSPL;

    void configure(const TextConfig& c) override { configs.push_back(c); }
    void writeLine(std::string_view s) override   { lines.emplace_back(s); }
    void writeBlank() override                    { lines.emplace_back(""); }
    PrintJob flush() override                     { return PrintJob{}; }
    void reset() override                         {}
    ProtocolType protocol() const override        { return proto; }
};

// Build a 40-byte record: content bytes, optional 0x9B EOL, space padding.
static std::vector<uint8_t> makeRecord(const std::vector<uint8_t>& bytes,
                                       bool addEol = true)
{
    std::vector<uint8_t> rec(Sio::RECORD_SIZE, 0x20);
    for (size_t i = 0; i < bytes.size() && i < Sio::RECORD_SIZE; ++i)
        rec[i] = bytes[i];
    if (addEol && bytes.size() < Sio::RECORD_SIZE)
        rec[bytes.size()] = Sio::ATASCII_EOL;
    return rec;
}

// Convenience: make a record with a single ESC ~ command followed by EOL.
static std::vector<uint8_t> escRecord(uint8_t cmd, uint8_t val)
{
    return makeRecord({0x1B, 0x7E, cmd, val});
}

// Convenience: ingest a record into the assembler.
static void ingest(LineAssembler& la, const std::vector<uint8_t>& rec)
{
    la.ingest(rec.data(), static_cast<uint8_t>(rec.size()));
}

// ── ESC ~ W ────────────────────────────────────────────────────────────────

TEST_CASE("ESC ~ W sets label width in dots", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('W', 50));   // 50 mm → 400 dots
    // config-only record: no writeLine yet
    REQUIRE(gen.lines.empty());

    // Next text record triggers configure() then writeLine()
    ingest(la, makeRecord({'H', 'i'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelWidthDots == 50 * 8);
    REQUIRE(gen.lines.size() == 1);
    CHECK(gen.lines[0] == "Hi");
}

TEST_CASE("ESC ~ W val=0 is rejected", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    TextConfig def;
    ingest(la, escRecord('W', 0));
    ingest(la, makeRecord({'X'}));
    // configure() should NOT have been called (dirty flag not set)
    CHECK(gen.configs.empty());
}

// ── ESC ~ H ────────────────────────────────────────────────────────────────

TEST_CASE("ESC ~ H sets label height in dots", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('H', 30));   // 30 mm → 240 dots
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelHeightDots == 30 * 8);
}

TEST_CASE("ESC ~ H val=0 is rejected", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('H', 0));
    ingest(la, makeRecord({'A'}));
    CHECK(gen.configs.empty());
}

// ── ESC ~ G ────────────────────────────────────────────────────────────────

TEST_CASE("ESC ~ G sets gap dots (rounded)", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    // val=30 → (30*8+5)/10 = 245/10 = 24 dots
    ingest(la, escRecord('G', 30));
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].gapDots == 24);
}

TEST_CASE("ESC ~ G val=0 sets gap to 0 (valid — gapless)", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('G', 0));
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].gapDots == 0);
}

// ── ESC ~ F ────────────────────────────────────────────────────────────────

TEST_CASE("ESC ~ F sets font ID (valid range 1-8)", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('F', 5));
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].tsplFontId == 5);
}

TEST_CASE("ESC ~ F val=0 is rejected", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('F', 0));
    ingest(la, makeRecord({'A'}));
    CHECK(gen.configs.empty());
}

TEST_CASE("ESC ~ F val=9 is rejected", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('F', 9));
    ingest(la, makeRecord({'A'}));
    CHECK(gen.configs.empty());
}

// ── ESC ~ X / Y ────────────────────────────────────────────────────────────

TEST_CASE("ESC ~ X sets font X multiplier (valid range 1-8)", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('X', 2));
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].tsplFontXMul == 2);
}

TEST_CASE("ESC ~ X val=0 is rejected", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('X', 0));
    ingest(la, makeRecord({'A'}));
    CHECK(gen.configs.empty());
}

TEST_CASE("ESC ~ X val=9 is rejected", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('X', 9));
    ingest(la, makeRecord({'A'}));
    CHECK(gen.configs.empty());
}

TEST_CASE("ESC ~ Y sets font Y multiplier", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('Y', 3));
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].tsplFontYMul == 3);
}

// ── ESC ~ L / T ────────────────────────────────────────────────────────────

TEST_CASE("ESC ~ L sets left margin in dots", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('L', 5));   // 5 mm → 40 dots
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].marginLeftDots == 40);
}

TEST_CASE("ESC ~ L val=0 sets zero margin (valid)", "[lineassembler_esc]") {
    TrackingGenerator gen;
    // Start with non-zero margin via defaultConfig
    TextConfig def;
    def.marginLeftDots = 80;
    LineAssembler la(LineAssembler::Mode::COL_40, gen, def);
    ingest(la, escRecord('L', 0));
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].marginLeftDots == 0);
}

TEST_CASE("ESC ~ T sets top margin in dots", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);
    ingest(la, escRecord('T', 2));   // 2 mm → 16 dots
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].marginTopDots == 16);
}

// ── ESC ~ D (reset to compile-time defaults) ──────────────────────────────

TEST_CASE("ESC ~ D 0 resets all config to defaults", "[lineassembler_esc]") {
    TrackingGenerator gen;
    TextConfig def;   // compiled defaults
    LineAssembler la(LineAssembler::Mode::COL_40, gen, def);

    // Set some non-default values
    ingest(la, escRecord('W', 72));
    ingest(la, escRecord('X', 2));
    // Reset to defaults
    ingest(la, escRecord('D', 0));
    ingest(la, makeRecord({'A'}));

    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelWidthDots  == def.labelWidthDots);
    CHECK(gen.configs[0].tsplFontXMul    == def.tsplFontXMul);
}

TEST_CASE("ESC ~ D with nonzero val is ignored", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('W', 72));   // sets dirty
    ingest(la, escRecord('D', 1));    // invalid (val != 0) — ignored
    ingest(la, makeRecord({'A'}));

    // Width change still in effect (D was ignored)
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelWidthDots == 72 * 8);
}

// ── Config-only records ────────────────────────────────────────────────────

TEST_CASE("Config-only record produces no writeLine call", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('W', 58));
    CHECK(gen.lines.empty());
    CHECK(gen.configs.empty());   // configure() deferred until next writeLine
}

TEST_CASE("configure() called exactly once before writeLine", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    // Two ESC commands in separate records
    ingest(la, escRecord('W', 58));
    ingest(la, escRecord('X', 2));
    // One text record — should trigger exactly one configure() call
    ingest(la, makeRecord({'H', 'i'}));

    CHECK(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelWidthDots == 58 * 8);
    CHECK(gen.configs[0].tsplFontXMul   == 2);
    CHECK(gen.lines.size() == 1);
}

TEST_CASE("Config persists across multiple text records", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('W', 58));
    ingest(la, makeRecord({'L', 'i', 'n', 'e', '1'}));  // triggers configure
    ingest(la, makeRecord({'L', 'i', 'n', 'e', '2'}));  // no new configure

    // configure() only called once (dirty cleared after first writeLine)
    CHECK(gen.configs.size() == 1);
    CHECK(gen.lines.size() == 2);
}

// ── Non-TSPL generator — ESC sequences silently ignored ───────────────────

TEST_CASE("ESC sequences ignored for non-TSPL generator", "[lineassembler_esc]") {
    TrackingGenerator gen;
    gen.proto = ProtocolType::ESCPOS;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('W', 58));
    ingest(la, makeRecord({'A'}));

    // configure() must never be called for non-TSPL
    CHECK(gen.configs.empty());
    // For TSPL: ESC ~ sequences are consumed silently (4 bytes skipped, result is empty → no writeLine).
    // For ESCPOS: empty processed record → writeBlank() (advances paper), then 'A' → writeLine().
    // Both records produce output: 1 blank + 1 text line = 2 entries.
    CHECK(gen.lines.size() == 2);
}

// ── Mixed ESC + text in same record ───────────────────────────────────────

TEST_CASE("ESC sequence mixed with text updates config and emits text", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    // Record: ESC ~ X 2 then "Hi" then EOL
    std::vector<uint8_t> rec = makeRecord({0x1B, 0x7E, 'X', 2, 'H', 'i'});
    ingest(la, rec);

    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].tsplFontXMul == 2);
    REQUIRE(gen.lines.size() == 1);
    CHECK(gen.lines[0] == "Hi");
}

// ── Incomplete ESC sequence (boundary) ────────────────────────────────────

TEST_CASE("Incomplete ESC sequence (3 bytes before EOL) falls through", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    // ESC(0x1B) ~ W  — only 3 bytes before EOL, not 4
    // effectiveLen = 3, condition (i+3 < 3) at i=0 is false → falls through
    std::vector<uint8_t> rec = makeRecord({0x1B, 0x7E, 'W'});
    ingest(la, rec);   // must not crash

    // No config change
    CHECK(gen.configs.empty());
    // writeLine IS called with garbage (0x1B→space, 0x7E→'~', 'W'→'W' = " ~W" → stripped leading spaces? No — only trailing)
    // leading space stays: " ~W"
    CHECK(gen.lines.size() == 1);
}

TEST_CASE("ESC alone at end of record falls through safely", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    std::vector<uint8_t> rec = makeRecord({0x1B});
    ingest(la, rec);   // must not crash, no config change
    CHECK(gen.configs.empty());
}

// ── defaultConfig passed to constructor ───────────────────────────────────

TEST_CASE("defaultConfig respected: ESC ~ D 0 resets to constructor arg", "[lineassembler_esc]") {
    TrackingGenerator gen;
    TextConfig custom;
    custom.labelWidthDots  = 464;  // 58 mm
    custom.labelHeightDots = 4800; // long roll
    LineAssembler la(LineAssembler::Mode::COL_40, gen, custom);

    // Change something
    ingest(la, escRecord('W', 100));
    // Reset to the constructor-provided defaults
    ingest(la, escRecord('D', 0));
    ingest(la, makeRecord({'A'}));

    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelWidthDots  == 464);
    CHECK(gen.configs[0].labelHeightDots == 4800);
}

// ── COL_80 mode ───────────────────────────────────────────────────────────

TEST_CASE("COL_80: config-only first record does not arm half-line buffer", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_80, gen);

    // First record: config only (with EOL so eolFound is true)
    ingest(la, escRecord('W', 58));

    // Second record: text (treated as a new first record, not joined with config)
    ingest(la, makeRecord({'H', 'e', 'l', 'l', 'o'}));

    // No lines yet (waiting for the paired second record in COL_80)
    // Actually: config record had EOL → eolFound=true → no halfPending armed
    //           Text record has EOL → eolFound=true → emitted immediately
    REQUIRE(gen.configs.size() == 1);
    REQUIRE(gen.lines.size() == 1);
    CHECK(gen.lines[0] == "Hello");
}

TEST_CASE("COL_80: ESC in second record updates config and emits joined line", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_80, gen);

    // First record: text, no EOL (arms half-line buffer)
    std::vector<uint8_t> first(Sio::RECORD_SIZE, 'A');  // 40 'A's, no EOL
    ingest(la, first);

    // Second record: ESC command + text "B" + EOL
    ingest(la, makeRecord({0x1B, 0x7E, 'X', 2, 'B'}));

    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].tsplFontXMul == 2);
    REQUIRE(gen.lines.size() == 1);
    // Joined: 40 'A's + "B"
    CHECK(gen.lines[0].front() == 'A');
    CHECK(gen.lines[0].back()  == 'B');
    CHECK(gen.lines[0].size()  == 41);
}

// ── ESC ~ P (print trigger) ────────────────────────────────────────────────

TEST_CASE("ESC ~ P sets takePrintRequest() to true", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    CHECK_FALSE(la.takePrintRequest());   // initially false
    ingest(la, escRecord('P', 0));
    CHECK(la.takePrintRequest());         // now true
    CHECK_FALSE(la.takePrintRequest());   // consumed — back to false
}

TEST_CASE("ESC ~ P value byte is ignored (any val works)", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('P', 99));
    CHECK(la.takePrintRequest());
}

TEST_CASE("ESC ~ P config-only record produces no writeLine", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('P', 0));
    CHECK(gen.lines.empty());
    CHECK(la.takePrintRequest());
}

TEST_CASE("ESC ~ P mixed with text: flag set and text emitted", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    // ESC ~ P followed by text in same record
    ingest(la, makeRecord({0x1B, 0x7E, 'P', 0, 'H', 'i'}));

    CHECK(la.takePrintRequest());
    REQUIRE(gen.lines.size() == 1);
    CHECK(gen.lines[0] == "Hi");
}

TEST_CASE("ESC ~ P combined with config change in same record", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    // ESC ~ W 58 then ESC ~ P 0, no text
    ingest(la, makeRecord({0x1B, 0x7E, 'W', 58, 0x1B, 0x7E, 'P', 0}));

    CHECK(la.takePrintRequest());
    // No text → no writeLine; config dirty but deferred
    CHECK(gen.lines.empty());
    // Config applied on next writeLine
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelWidthDots == 58 * 8);
}

TEST_CASE("ESC ~ P ignored for non-TSPL generator", "[lineassembler_esc]") {
    TrackingGenerator gen;
    gen.proto = ProtocolType::ESCPOS;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('P', 0));
    // Flag NOT set for non-TSPL
    CHECK_FALSE(la.takePrintRequest());
}

// ── ESC ~ R (load from flash slot) ────────────────────────────────────────

TEST_CASE("ESC ~ R 0 sets takeLoadRequest slot=0", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    CHECK_FALSE(la.takeLoadRequest(slot));   // initially false

    ingest(la, escRecord('R', 0));
    CHECK(la.takeLoadRequest(slot));
    CHECK(slot == 0);
    CHECK_FALSE(la.takeLoadRequest(slot));   // consumed
}

TEST_CASE("ESC ~ R 9 sets takeLoadRequest slot=9", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    ingest(la, escRecord('R', 9));
    CHECK(la.takeLoadRequest(slot));
    CHECK(slot == 9);
}

TEST_CASE("ESC ~ R 10 (out of range) is ignored", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    ingest(la, escRecord('R', 10));
    CHECK_FALSE(la.takeLoadRequest(slot));
}

TEST_CASE("ESC ~ R sets load flag independently of config dirty flag", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('R', 5));

    uint8_t slot = 0xFF;
    REQUIRE(la.takeLoadRequest(slot));
    CHECK(slot == 5);
    // Config not changed by R itself
    CHECK(gen.configs.empty());
    CHECK(gen.lines.empty());
}

TEST_CASE("ESC ~ R ignored for non-TSPL generator", "[lineassembler_esc]") {
    TrackingGenerator gen;
    gen.proto = ProtocolType::ESCPOS;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    ingest(la, escRecord('R', 0));
    CHECK_FALSE(la.takeLoadRequest(slot));
}

// ── ESC ~ S (save to flash slot) ──────────────────────────────────────────

TEST_CASE("ESC ~ S 0 sets takeSaveRequest slot=0", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    CHECK_FALSE(la.takeSaveRequest(slot));   // initially false

    ingest(la, escRecord('S', 0));
    CHECK(la.takeSaveRequest(slot));
    CHECK(slot == 0);
    CHECK_FALSE(la.takeSaveRequest(slot));   // consumed
}

TEST_CASE("ESC ~ S 5 sets takeSaveRequest slot=5", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    ingest(la, escRecord('S', 5));
    CHECK(la.takeSaveRequest(slot));
    CHECK(slot == 5);
}

TEST_CASE("ESC ~ S 10 (out of range) is ignored", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    ingest(la, escRecord('S', 10));
    CHECK_FALSE(la.takeSaveRequest(slot));
}

TEST_CASE("ESC ~ S config-only record produces no writeLine", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('S', 0));
    CHECK(gen.lines.empty());

    uint8_t slot = 0xFF;
    CHECK(la.takeSaveRequest(slot));
    CHECK(slot == 0);
}

TEST_CASE("ESC ~ S ignored for non-TSPL generator", "[lineassembler_esc]") {
    TrackingGenerator gen;
    gen.proto = ProtocolType::ESCPOS;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    uint8_t slot = 0xFF;
    ingest(la, escRecord('S', 0));
    CHECK_FALSE(la.takeSaveRequest(slot));
}

// ── setConfig / getConfig ─────────────────────────────────────────────────

TEST_CASE("setConfig propagates to getConfig and marks dirty", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    TextConfig loaded;
    loaded.labelWidthDots  = 464;
    loaded.labelHeightDots = 240;
    loaded.tsplFontXMul    = 3;

    la.setConfig(loaded);
    CHECK(la.getConfig().labelWidthDots == 464);
    CHECK(la.getConfig().tsplFontXMul   == 3);

    // Next writeLine should call configure() with the loaded config
    ingest(la, makeRecord({'A'}));
    REQUIRE(gen.configs.size() == 1);
    CHECK(gen.configs[0].labelWidthDots == 464);
    CHECK(gen.configs[0].tsplFontXMul   == 3);
}

TEST_CASE("getConfig reflects most recent ESC ~ changes", "[lineassembler_esc]") {
    TrackingGenerator gen;
    LineAssembler la(LineAssembler::Mode::COL_40, gen);

    ingest(la, escRecord('W', 58));
    CHECK(la.getConfig().labelWidthDots == 58 * 8);

    ingest(la, escRecord('X', 2));
    CHECK(la.getConfig().tsplFontXMul == 2);
}
