#include <catch2/catch_test_macros.hpp>
#include "sio/SioPrinterEmulator.h"
#include "sio/SioProtocol.h"
#include "generator/ITextGenerator.h"
#include "mock/MockSioPort.h"
#include <vector>
#include <string>

// ── Test double generators ────────────────────────────────────────────────────

// Captures writeLine() calls; flush() returns empty job
class CapturingGenerator : public ITextGenerator {
public:
    std::vector<std::string> lines;
    int                      flushCount = 0;

    void configure(const TextConfig&) override {}
    void writeLine(std::string_view s) override { lines.emplace_back(s); }
    void writeBlank() override { lines.emplace_back(""); }
    PrintJob flush() override { ++flushCount; return PrintJob{}; }
    void reset() override { lines.clear(); }
    ProtocolType protocol() const override { return ProtocolType::ESCP; }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::vector<uint8_t> makeWriteFrame(uint8_t deviceId = Sio::DEVICE_P1,
                                             uint8_t aux1 = 'N') {
    uint8_t frame[4] = {deviceId, Sio::CMD_WRITE, aux1, 0x00};
    uint8_t cs = Sio::checksum(frame, 4);
    return {frame[0], frame[1], frame[2], frame[3], cs};
}

static std::vector<uint8_t> makePutFrame(uint8_t deviceId = Sio::DEVICE_P1,
                                          uint8_t aux1 = 'N') {
    uint8_t frame[4] = {deviceId, Sio::CMD_PUT, aux1, 0x00};
    uint8_t cs = Sio::checksum(frame, 4);
    return {frame[0], frame[1], frame[2], frame[3], cs};
}

static std::vector<uint8_t> makeStatusFrame(uint8_t deviceId = Sio::DEVICE_P1) {
    uint8_t frame[4] = {deviceId, Sio::CMD_STATUS, 0x00, 0x00};
    uint8_t cs = Sio::checksum(frame, 4);
    return {frame[0], frame[1], frame[2], frame[3], cs};
}

static std::vector<uint8_t> makeRecord(const std::string& text,
                                        uint8_t aux1 = 'N') {
    uint8_t len = Sio::recordLength(aux1);
    std::vector<uint8_t> data(len, ' ');
    for (size_t i = 0; i < text.size() && i < len; ++i)
        data[i] = static_cast<uint8_t>(text[i]);
    if (text.size() < len)
        data[text.size()] = Sio::ATASCII_EOL;

    uint8_t cs = Sio::checksum(data.data(), len);
    data.push_back(cs);
    return data;
}

// Drive a STATUS cycle. Returns txLog.
// Expected: {ACK, COMPLETE, s0, s1, s2, s3, checksum} = 7 bytes.
static std::vector<uint8_t> runStatusCycle(MockSioPort& port,
                                            SioPrinterEmulator& emu,
                                            uint8_t deviceId = Sio::DEVICE_P1) {
    port.clearTx();

    port.cmdAsserted = true;
    emu.tick(); // IDLE → RECV_CMD_FRAME

    auto frame = makeStatusFrame(deviceId);
    port.enqueue(frame.data(), frame.size());
    emu.tick(); // RECV_CMD_FRAME → SEND_ACK_CMD

    emu.tick(); // SEND_ACK_CMD → SEND_STATUS
    emu.tick(); // SEND_STATUS → IDLE

    return port.txLog;
}

// Drive a full CMD+RECORD write/put cycle through the emulator.
// Returns the bytes sent by the emulator (txLog).
static std::vector<uint8_t> runOneCycle(MockSioPort& port,
                                         SioPrinterEmulator& emu,
                                         const std::string& text,
                                         uint8_t deviceId = Sio::DEVICE_P1,
                                         uint8_t aux1 = 'N',
                                         bool put = false) {
    port.clearTx();

    port.cmdAsserted = true;
    emu.tick(); // IDLE → RECV_CMD_FRAME

    auto frame = put ? makePutFrame(deviceId, aux1) : makeWriteFrame(deviceId, aux1);
    port.enqueue(frame.data(), frame.size());
    emu.tick(); // RECV_CMD_FRAME → SEND_ACK_CMD

    emu.tick(); // SEND_ACK_CMD → RECV_RECORD

    auto rec = makeRecord(text, aux1);
    port.enqueue(rec.data(), rec.size());
    emu.tick(); // RECV_RECORD → SEND_ACK_DATA

    emu.tick(); // SEND_ACK_DATA → PROCESS_RECORD
    emu.tick(); // PROCESS_RECORD → SEND_COMPLETE
    emu.tick(); // SEND_COMPLETE → IDLE

    return port.txLog;
}

// ── Valid write cycle ─────────────────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: valid write → ACK, ACK, COMPLETE", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runOneCycle(port, emu, "HELLO");

    REQUIRE(tx.size() == 3);
    REQUIRE(tx[0] == Sio::ACK);
    REQUIRE(tx[1] == Sio::ACK);
    REQUIRE(tx[2] == Sio::COMPLETE);
}

TEST_CASE("SioPrinterEmulator: valid write passes text to generator", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    runOneCycle(port, emu, "WORLD");

    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0] == "WORLD");
}

TEST_CASE("SioPrinterEmulator: two consecutive writes", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    runOneCycle(port, emu, "FIRST");
    runOneCycle(port, emu, "SECOND");

    REQUIRE(gen.lines.size() == 2);
    REQUIRE(gen.lines[0] == "FIRST");
    REQUIRE(gen.lines[1] == "SECOND");
}

// ── Bad checksum → NAK ────────────────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: bad command frame checksum → NAK + IDLE", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    port.cmdAsserted = true;
    emu.tick(); // IDLE → RECV_CMD_FRAME

    auto frame = makeWriteFrame();
    frame[4] ^= 0xFF; // corrupt checksum
    port.enqueue(frame.data(), frame.size());
    emu.tick(); // → NAK → IDLE

    REQUIRE(port.txLog.size() == 1);
    REQUIRE(port.txLog[0] == Sio::NAK);

    // Confirm returned to IDLE: next tick with no CMD does nothing
    port.clearTx();
    emu.tick();
    REQUIRE(port.txLog.empty());
}

TEST_CASE("SioPrinterEmulator: bad record checksum → NAK + IDLE", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    // Advance through cmd frame
    port.cmdAsserted = true;
    emu.tick();
    auto frame = makeWriteFrame();
    port.enqueue(frame.data(), frame.size());
    emu.tick(); // → SEND_ACK_CMD
    emu.tick(); // → RECV_RECORD

    // Bad record checksum
    auto rec = makeRecord("TEST");
    rec[Sio::RECORD_SIZE] ^= 0xFF;
    port.enqueue(rec.data(), rec.size());
    emu.tick(); // → NAK → IDLE

    // txLog: ACK (cmd) + NAK (bad record)
    REQUIRE(port.txLog.size() == 2);
    REQUIRE(port.txLog[0] == Sio::ACK);
    REQUIRE(port.txLog[1] == Sio::NAK);
    REQUIRE(gen.lines.empty()); // nothing ingested
}

// ── Timeout → NAK ────────────────────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: timeout reading command frame → NAK", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    port.cmdAsserted = true;
    emu.tick(); // IDLE → RECV_CMD_FRAME

    port.alwaysTimeout = true;
    emu.tick(); // timeout → NAK → IDLE

    REQUIRE(port.txLog.size() == 1);
    REQUIRE(port.txLog[0] == Sio::NAK);
}

// ── Non-printer device / unsupported command ──────────────────────────────────

TEST_CASE("SioPrinterEmulator: non-printer device ID ignored", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    port.cmdAsserted = true;
    emu.tick();

    // Device ID $31 = disk drive
    uint8_t frame[4] = {0x31, Sio::CMD_WRITE, 0x00, 0x00};
    uint8_t cs = Sio::checksum(frame, 4);
    port.enqueue({frame[0], frame[1], frame[2], frame[3], cs});
    emu.tick(); // not printer → IDLE, no NAK/ACK

    REQUIRE(port.txLog.empty());
}

TEST_CASE("SioPrinterEmulator: unsupported command → NAK", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    port.cmdAsserted = true;
    emu.tick();

    // Command $52 = 'R' (read)
    uint8_t frame[4] = {Sio::DEVICE_P1, 0x52, 0x00, 0x00};
    uint8_t cs = Sio::checksum(frame, 4);
    port.enqueue({frame[0], frame[1], frame[2], frame[3], cs});
    emu.tick(); // unsupported → NAK → IDLE

    REQUIRE(port.txLog.size() == 1);
    REQUIRE(port.txLog[0] == Sio::NAK);
}

// ── No CMD → stays IDLE ───────────────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: no CMD line → stays IDLE, no output", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    for (int i = 0; i < 10; ++i) emu.tick();
    REQUIRE(port.txLog.empty());
}

// ── Printer 2 (device $41) ────────────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: printer device $41 accepted", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runOneCycle(port, emu, "P2", Sio::DEVICE_P2);
    REQUIRE(tx.size() == 3);
    REQUIRE(tx[0] == Sio::ACK);
    REQUIRE(tx[2] == Sio::COMPLETE);
}

// ── STATUS command ────────────────────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: STATUS returns ACK + COMPLETE + 5 bytes", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runStatusCycle(port, emu);

    // ACK, COMPLETE, status[0..3], checksum
    REQUIRE(tx.size() == 7);
    REQUIRE(tx[0] == Sio::ACK);
    REQUIRE(tx[1] == Sio::COMPLETE);
    REQUIRE(tx[2] == 0x00);  // done/error flag
    REQUIRE(tx[4] == 0x05);  // write timeout constant
    REQUIRE(tx[5] == 0x00);  // unused
    // verify checksum
    uint8_t cs = Sio::checksum(&tx[2], 4);
    REQUIRE(tx[6] == cs);
}

TEST_CASE("SioPrinterEmulator: STATUS byte[1] is 0 when no prior WRITE", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runStatusCycle(port, emu);
    REQUIRE(tx[3] == 0x00);  // lastAux1 not yet set
}

TEST_CASE("SioPrinterEmulator: STATUS byte[1] reflects AUX1 from last WRITE", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    // WRITE with AUX1='N'
    runOneCycle(port, emu, "HELLO", Sio::DEVICE_P1, 'N');

    auto tx = runStatusCycle(port, emu);
    REQUIRE(tx[3] == 'N');
}

// ── PUT command ───────────────────────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: PUT accepted, produces same output as WRITE", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runOneCycle(port, emu, "GRFX", Sio::DEVICE_P1, 'N', /*put=*/true);

    REQUIRE(tx.size() == 3);
    REQUIRE(tx[0] == Sio::ACK);
    REQUIRE(tx[1] == Sio::ACK);
    REQUIRE(tx[2] == Sio::COMPLETE);
    REQUIRE(gen.lines.size() == 1);
    REQUIRE(gen.lines[0] == "GRFX");
}

// ── Variable record length (AUX1) ─────────────────────────────────────────────

TEST_CASE("SioPrinterEmulator: AUX1='N' (40 bytes) accepted", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runOneCycle(port, emu, "NORMAL", Sio::DEVICE_P1, 'N');
    REQUIRE(tx.size() == 3);
    REQUIRE(tx[2] == Sio::COMPLETE);
    REQUIRE(!gen.lines.empty());
}

TEST_CASE("SioPrinterEmulator: AUX1='S' (29 bytes sideways) accepted", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runOneCycle(port, emu, "SIDEWAYS", Sio::DEVICE_P1, 'S');
    // Should complete without NAK
    REQUIRE(tx.size() == 3);
    REQUIRE(tx[0] == Sio::ACK);
    REQUIRE(tx[2] == Sio::COMPLETE);
}

TEST_CASE("SioPrinterEmulator: AUX1='D' (20 bytes double-wide) accepted", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    auto tx = runOneCycle(port, emu, "DOUBLE", Sio::DEVICE_P1, 'D');
    REQUIRE(tx.size() == 3);
    REQUIRE(tx[0] == Sio::ACK);
    REQUIRE(tx[2] == Sio::COMPLETE);
}

TEST_CASE("SioPrinterEmulator: AUX1 unknown defaults to 40 bytes", "[sio_emu]") {
    MockSioPort port;
    CapturingGenerator gen;
    SioPrinterEmulator emu(port, gen, LineAssembler::Mode::COL_40);

    // aux1=0x00 is not 'N', 'S', or 'D' → should default to 40 bytes
    auto tx = runOneCycle(port, emu, "DEFAULT", Sio::DEVICE_P1, 0x00);
    REQUIRE(tx.size() == 3);
    REQUIRE(tx[0] == Sio::ACK);
    REQUIRE(tx[2] == Sio::COMPLETE);
}
