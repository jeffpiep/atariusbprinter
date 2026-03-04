#include <catch2/catch_test_macros.hpp>
#include "protocol/EscpHandler.h"
#include "mock/MockUsbTransport.h"
#include <vector>

// ── probe() tests ─────────────────────────────────────────────────────────────

TEST_CASE("EscpHandler::probe - ESC @ (initialize)", "[escp][probe]") {
    EscpHandler h;
    const uint8_t data[] = {0x1B, '@'};
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscpHandler::probe - ESC P (select printer)", "[escp][probe]") {
    EscpHandler h;
    const uint8_t data[] = {0x1B, 0x50}; // 0x50 = 'P'
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscpHandler::probe - GS class ESC/POS", "[escp][probe]") {
    EscpHandler h;
    const uint8_t data[] = {0x1D, 'V', 0x00}; // GS V = paper cut
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscpHandler::probe - ESC @ within first 8 bytes", "[escp][probe]") {
    EscpHandler h;
    // 5 bytes padding, then ESC @
    const uint8_t data[] = {'X','X','X','X','X', 0x1B, '@'};
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscpHandler::probe - TSPL data rejected", "[escp][probe]") {
    EscpHandler h;
    const uint8_t data[] = {'S','I','Z','E',' ','1','0','0',' ','m','m'};
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscpHandler::probe - PCL data rejected", "[escp][probe]") {
    EscpHandler h;
    const uint8_t data[] = {0x1B, 'E', 0x1B, '&', 'l', '0', 'O'}; // ESC E = PCL
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscpHandler::probe - PJL data rejected", "[escp][probe]") {
    EscpHandler h;
    const uint8_t data[] = {0x1B,'%','-','1','2','3','4','5','X'};
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscpHandler::probe - null / empty rejected", "[escp][probe]") {
    EscpHandler h;
    REQUIRE_FALSE(h.probe(nullptr, 10));
    const uint8_t data[] = {0x1B, '@'};
    REQUIRE_FALSE(h.probe(data, 0));
}

TEST_CASE("EscpHandler::probe - ESC @ beyond byte 8 rejected", "[escp][probe]") {
    EscpHandler h;
    // 8 bytes of padding, then ESC @ at offset 8 — outside window
    uint8_t data[10];
    for (size_t i = 0; i < 8; ++i) data[i] = 'X';
    data[8] = 0x1B; data[9] = '@';
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

// ── sendJob() tests ────────────────────────────────────────────────────────────

TEST_CASE("EscpHandler::sendJob - sends all bytes", "[escp][send]") {
    MockUsbTransport t;
    EscpHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "escp_test";
    job.timeoutMs = 1000;
    job.rawData = {0x1B, '@', 'H', 'i', '\r', '\n', '\x0c'};

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes() == job.rawData);
}

TEST_CASE("EscpHandler::sendJob - empty job succeeds without write", "[escp][send]") {
    MockUsbTransport t;
    EscpHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "empty";
    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().empty());
}

TEST_CASE("EscpHandler::sendJob - chunks large job", "[escp][send]") {
    MockUsbTransport t;
    EscpHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "big";
    job.timeoutMs = 5000;
    job.rawData.assign(EscpHandler::ESCP_CHUNK_SIZE + 50, 0x41);

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().size() == job.rawData.size());
    REQUIRE(t.writeCallCount() == 2);
}

TEST_CASE("EscpHandler::sendJob - write failure returns false", "[escp][send]") {
    MockConfig cfg;
    cfg.failOnWrite = true;
    MockUsbTransport t(cfg);
    EscpHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "fail";
    job.rawData = {0x1B, '@', '\r', '\n'};
    REQUIRE_FALSE(h.sendJob(t, job));
}

// ── queryStatus() tests ────────────────────────────────────────────────────────

TEST_CASE("EscpHandler::queryStatus - returns status bytes", "[escp][status]") {
    MockConfig cfg;
    cfg.statusResponse = {0x00}; // online, no error
    MockUsbTransport t(cfg);
    EscpHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    std::string status;
    REQUIRE(h.queryStatus(t, status));
    REQUIRE(status.size() == 1);
    REQUIRE(status[0] == 0x00);
}

TEST_CASE("EscpHandler::queryStatus - write-only device returns false", "[escp][status]") {
    MockConfig cfg;
    cfg.writeOnly = true;
    MockUsbTransport t(cfg);
    EscpHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    std::string status;
    REQUIRE_FALSE(h.queryStatus(t, status));
}
