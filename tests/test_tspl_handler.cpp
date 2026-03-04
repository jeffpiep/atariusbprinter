#include <catch2/catch_test_macros.hpp>
#include "protocol/TsplHandler.h"
#include "mock/MockUsbTransport.h"
#include <cstring>
#include <string>
#include <vector>

// ─── probe() tests ──────────────────────────────────────────────────────────

TEST_CASE("TsplHandler::probe - SIZE command", "[tspl][probe]") {
    TsplHandler h;
    const uint8_t data[] = "SIZE 100 mm, 50 mm\r\nGAP 3 mm\r\n";
    REQUIRE(h.probe(data, strlen(reinterpret_cast<const char*>(data))));
}

TEST_CASE("TsplHandler::probe - ZPL ^XA preamble", "[tspl][probe]") {
    TsplHandler h;
    const uint8_t data[] = "^XA^FO50,50^ADN,36,20^FDHello^FS^XZ";
    REQUIRE(h.probe(data, strlen(reinterpret_cast<const char*>(data))));
}

TEST_CASE("TsplHandler::probe - ESC-@ reset", "[tspl][probe]") {
    TsplHandler h;
    const uint8_t data[] = {0x1B, 0x40, 'S', 'I', 'Z', 'E'};
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("TsplHandler::probe - negative: PCL stream", "[tspl][probe]") {
    TsplHandler h;
    const uint8_t data[] = {0x1B, 'E', 0x1B, '&', 'l', '0', 'O'};
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("TsplHandler::probe - negative: ESC/P stream", "[tspl][probe]") {
    TsplHandler h;
    const uint8_t data[] = {0x1B, '@', 0x1B, 'x', '1'};
    // Note: 0x1B 0x40 IS the TSPL ESC-@ reset, but also ESC/P init.
    // The spec says 0x1B 0x40 is a TSPL probe match. If ESC/P also uses
    // it, registration order (TSPL registered first) resolves ambiguity.
    // Here we just verify TSPL probe fires on it.
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("TsplHandler::probe - negative: random binary", "[tspl][probe]") {
    TsplHandler h;
    const uint8_t data[] = {0x00, 0xFF, 0xAB, 0xCD};
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("TsplHandler::probe - negative: empty data", "[tspl][probe]") {
    TsplHandler h;
    REQUIRE_FALSE(h.probe(nullptr, 0));
    const uint8_t data[] = {0x00};
    REQUIRE_FALSE(h.probe(data, 0));
}

// ─── sendJob() tests ─────────────────────────────────────────────────────────

TEST_CASE("TsplHandler::sendJob - sends all bytes", "[tspl][sendJob]") {
    TsplHandler h;
    MockUsbTransport t;
    t.open({});

    PrintJob job;
    job.jobId = "test1";
    std::string tspl = "SIZE 100 mm, 50 mm\r\nCLS\r\nPRINT 1,1\r\n";
    job.rawData.assign(tspl.begin(), tspl.end());

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().size() == tspl.size());

    std::string written(t.writtenBytes().begin(), t.writtenBytes().end());
    REQUIRE(written == tspl);
}

TEST_CASE("TsplHandler::sendJob - chunking", "[tspl][sendJob]") {
    TsplHandler h;
    MockUsbTransport t;
    t.open({});

    // Create a job larger than one chunk (4096 bytes)
    PrintJob job;
    job.jobId = "bigchunk";
    job.rawData.resize(10000, 'A');

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().size() == 10000);
    // Should have called write 3 times: 4096 + 4096 + 1808
    REQUIRE(t.writeCallCount() == 3);
}

TEST_CASE("TsplHandler::sendJob - write failure returns false", "[tspl][sendJob]") {
    TsplHandler h;
    MockConfig cfg;
    cfg.failOnWrite = true;
    MockUsbTransport t(cfg);
    t.open({});

    PrintJob job;
    job.jobId = "failtest";
    job.rawData = {0x1B, 0x40, 'S', 'I', 'Z', 'E'};

    REQUIRE_FALSE(h.sendJob(t, job));
}

TEST_CASE("TsplHandler::sendJob - empty job succeeds", "[tspl][sendJob]") {
    TsplHandler h;
    MockUsbTransport t;
    t.open({});

    PrintJob job;
    job.jobId = "empty";
    // Empty rawData

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().empty());
}

TEST_CASE("TsplHandler::sendJob - disconnect mid-job returns false", "[tspl][sendJob]") {
    TsplHandler h;
    MockConfig cfg;
    cfg.disconnectAfter      = true;
    cfg.disconnectWriteCount = 1; // fail on the 2nd write
    MockUsbTransport t(cfg);
    t.open({});

    PrintJob job;
    job.jobId = "disconnect";
    job.rawData.resize(8192, 'B'); // needs 2 chunks

    REQUIRE_FALSE(h.sendJob(t, job));
}

// ─── queryStatus() tests ─────────────────────────────────────────────────────

TEST_CASE("TsplHandler::queryStatus - returns status when available", "[tspl][status]") {
    TsplHandler h;
    MockConfig cfg;
    std::string resp = "OK\r\n";
    cfg.statusResponse.assign(resp.begin(), resp.end());
    MockUsbTransport t(cfg);
    t.open({});

    std::string status;
    REQUIRE(h.queryStatus(t, status));
    REQUIRE(status == resp);
}

TEST_CASE("TsplHandler::queryStatus - write-only device returns false gracefully", "[tspl][status]") {
    TsplHandler h;
    MockConfig cfg;
    cfg.writeOnly = true;
    MockUsbTransport t(cfg);
    t.open({});

    std::string status;
    bool result = h.queryStatus(t, status);
    // Non-fatal: write-only device should not crash
    REQUIRE_FALSE(result);
    REQUIRE(status.empty());
}