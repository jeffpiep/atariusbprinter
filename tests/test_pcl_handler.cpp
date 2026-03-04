#include <catch2/catch_test_macros.hpp>
#include "protocol/PclHandler.h"
#include "mock/MockUsbTransport.h"
#include <vector>
#include <cstring>

// ── probe() tests ─────────────────────────────────────────────────────────────

TEST_CASE("PclHandler::probe - PJL universal exit sequence", "[pcl][probe]") {
    PclHandler h;
    // ESC%-12345X
    const uint8_t data[] = {0x1B,'%','-','1','2','3','4','5','X'};
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("PclHandler::probe - ESC E reset at start", "[pcl][probe]") {
    PclHandler h;
    const uint8_t data[] = {0x1B, 'E', 0x1B, '&', 'l', '0', 'O'};
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("PclHandler::probe - ESC E within first 16 bytes", "[pcl][probe]") {
    PclHandler h;
    // 10 bytes of padding then ESC E
    const uint8_t data[] = {
        'X','X','X','X','X','X','X','X','X','X',
        0x1B, 'E'
    };
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("PclHandler::probe - ESC E beyond first 16 bytes rejected", "[pcl][probe]") {
    PclHandler h;
    // 16 bytes of padding, then ESC E at position 16 — outside window
    uint8_t data[18];
    memset(data, 'X', sizeof(data));
    data[16] = 0x1B;
    data[17] = 'E';
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("PclHandler::probe - TSPL data rejected", "[pcl][probe]") {
    PclHandler h;
    const uint8_t data[] = {'S','I','Z','E',' ','1','0','0',' ','m','m'};
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("PclHandler::probe - ESC/P data rejected", "[pcl][probe]") {
    PclHandler h;
    const uint8_t data[] = {0x1B, '@', 0x1B, 'x', 0x01}; // ESC @ = ESC/P init
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("PclHandler::probe - empty / null data rejected", "[pcl][probe]") {
    PclHandler h;
    REQUIRE_FALSE(h.probe(nullptr, 10));
    REQUIRE_FALSE(h.probe(nullptr, 0));
    const uint8_t data[] = {0x1B, 'E'};
    REQUIRE_FALSE(h.probe(data, 0));
}

TEST_CASE("PclHandler::probe - ESC/POS GS rejected", "[pcl][probe]") {
    PclHandler h;
    const uint8_t data[] = {0x1D, 'V', 0}; // GS V = ESC/POS cut
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

// ── sendJob() tests ────────────────────────────────────────────────────────────

TEST_CASE("PclHandler::sendJob - sends all bytes", "[pcl][send]") {
    MockUsbTransport t;
    PclHandler h;

    PrintJob job;
    job.jobId = "pcl_test";
    job.timeoutMs = 1000;
    job.rawData = {0x1B, 'E', 'H', 'e', 'l', 'l', 'o', 0x0C, 0x1B, 'E'};

    REQUIRE(t.open(UsbDeviceDescriptor{}));
    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes() == job.rawData);
}

TEST_CASE("PclHandler::sendJob - empty job succeeds without write", "[pcl][send]") {
    MockUsbTransport t;
    PclHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "empty";
    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().empty());
}

TEST_CASE("PclHandler::sendJob - chunks large job", "[pcl][send]") {
    MockUsbTransport t;
    PclHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    // Build a job slightly larger than 2 chunks
    PrintJob job;
    job.jobId = "big";
    job.timeoutMs = 5000;
    job.rawData.assign(PclHandler::PCL_CHUNK_SIZE * 2 + 100, 0x41); // 'A'

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().size() == job.rawData.size());
    REQUIRE(t.writeCallCount() == 3); // 4096 + 4096 + 100
}

TEST_CASE("PclHandler::sendJob - write failure returns false", "[pcl][send]") {
    MockConfig cfg;
    cfg.failOnWrite = true;
    MockUsbTransport t(cfg);
    PclHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "fail";
    job.rawData = {0x1B, 'E'};
    REQUIRE_FALSE(h.sendJob(t, job));
}

// ── queryStatus() tests ────────────────────────────────────────────────────────

TEST_CASE("PclHandler::queryStatus - returns status string", "[pcl][status]") {
    MockConfig cfg;
    cfg.statusResponse = {'O','K','\r','\n'};
    MockUsbTransport t(cfg);
    PclHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    std::string status;
    REQUIRE(h.queryStatus(t, status));
    REQUIRE(status == "OK\r\n");
}

TEST_CASE("PclHandler::queryStatus - write-only device returns false", "[pcl][status]") {
    MockConfig cfg;
    cfg.writeOnly = true;
    MockUsbTransport t(cfg);
    PclHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    std::string status;
    REQUIRE_FALSE(h.queryStatus(t, status));
}
