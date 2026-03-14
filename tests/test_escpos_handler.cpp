#include <catch2/catch_test_macros.hpp>
#include "protocol/EscposHandler.h"
#include "mock/MockUsbTransport.h"
#include <vector>

// ── probe() tests ─────────────────────────────────────────────────────────────

TEST_CASE("EscposHandler::probe - ESC @ (initialize)", "[escpos_handler][probe]") {
    EscposHandler h;
    const uint8_t data[] = {0x1B, '@'};
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscposHandler::probe - GS class ESC/POS", "[escpos_handler][probe]") {
    EscposHandler h;
    const uint8_t data[] = {0x1D, 'V', 0x00}; // GS V = paper cut
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscposHandler::probe - ESC @ within first 8 bytes", "[escpos_handler][probe]") {
    EscposHandler h;
    // 5 bytes padding, then ESC @
    const uint8_t data[] = {'X','X','X','X','X', 0x1B, '@'};
    REQUIRE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscposHandler::probe - ESC P (dot-matrix) rejected", "[escpos_handler][probe]") {
    EscposHandler h;
    const uint8_t data[] = {0x1B, 0x50}; // ESC P = ESC/P select printer
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscposHandler::probe - TSPL data rejected", "[escpos_handler][probe]") {
    EscposHandler h;
    const uint8_t data[] = {'S','I','Z','E',' ','1','0','0',' ','m','m'};
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscposHandler::probe - PCL data rejected", "[escpos_handler][probe]") {
    EscposHandler h;
    const uint8_t data[] = {0x1B, 'E', 0x1B, '&', 'l', '0', 'O'}; // ESC E = PCL
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

TEST_CASE("EscposHandler::probe - null / empty rejected", "[escpos_handler][probe]") {
    EscposHandler h;
    REQUIRE_FALSE(h.probe(nullptr, 10));
    const uint8_t data[] = {0x1B, '@'};
    REQUIRE_FALSE(h.probe(data, 0));
}

TEST_CASE("EscposHandler::probe - ESC @ beyond byte 8 rejected", "[escpos_handler][probe]") {
    EscposHandler h;
    // 8 bytes of padding, then ESC @ at offset 8 — outside window
    uint8_t data[10];
    for (size_t i = 0; i < 8; ++i) data[i] = 'X';
    data[8] = 0x1B; data[9] = '@';
    REQUIRE_FALSE(h.probe(data, sizeof(data)));
}

// ── sendJob() tests ────────────────────────────────────────────────────────────

TEST_CASE("EscposHandler::sendJob - sends all bytes", "[escpos_handler][send]") {
    MockUsbTransport t;
    EscposHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "escpos_test";
    job.timeoutMs = 1000;
    job.rawData = {0x1B, '@', 'H', 'i', '\n'};

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes() == job.rawData);
}

TEST_CASE("EscposHandler::sendJob - empty job succeeds without write", "[escpos_handler][send]") {
    MockUsbTransport t;
    EscposHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "empty";
    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().empty());
}

TEST_CASE("EscposHandler::sendJob - chunks large job", "[escpos_handler][send]") {
    MockUsbTransport t;
    EscposHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "big";
    job.timeoutMs = 5000;
    job.rawData.assign(EscposHandler::ESCPOS_CHUNK_SIZE + 50, 0x41);

    REQUIRE(h.sendJob(t, job));
    REQUIRE(t.writtenBytes().size() == job.rawData.size());
    REQUIRE(t.writeCallCount() == 2);
}

TEST_CASE("EscposHandler::sendJob - write failure returns false", "[escpos_handler][send]") {
    MockConfig cfg;
    cfg.failOnWrite = true;
    MockUsbTransport t(cfg);
    EscposHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    PrintJob job;
    job.jobId = "fail";
    job.rawData = {0x1B, '@', '\n'};
    REQUIRE_FALSE(h.sendJob(t, job));
}

// ── queryStatus() tests ────────────────────────────────────────────────────────

TEST_CASE("EscposHandler::queryStatus - sends DLE EOT 1", "[escpos_handler][status]") {
    MockConfig cfg;
    cfg.statusResponse = {0x12}; // typical printer status byte
    MockUsbTransport t(cfg);
    EscposHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    std::string status;
    REQUIRE(h.queryStatus(t, status));
    // Verify the correct ESC/POS real-time status command was sent
    const std::vector<uint8_t> expected = {0x10, 0x04, 0x01};
    REQUIRE(t.writtenBytes() == expected);
    REQUIRE(status.size() == 1);
}

TEST_CASE("EscposHandler::queryStatus - write-only device returns false", "[escpos_handler][status]") {
    MockConfig cfg;
    cfg.writeOnly = true;
    MockUsbTransport t(cfg);
    EscposHandler h;
    REQUIRE(t.open(UsbDeviceDescriptor{}));

    std::string status;
    REQUIRE_FALSE(h.queryStatus(t, status));
}
