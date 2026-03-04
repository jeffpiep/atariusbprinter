#include <catch2/catch_test_macros.hpp>
#include "manager/PrinterManager.h"
#include "protocol/TsplHandler.h"
#include "mock/MockUsbTransport.h"
#include <memory>
#include <string>

// Helper: build a TSPL job with given raw content
static PrintJob makeTsplJob(const std::string& content) {
    PrintJob job;
    job.jobId = "test";
    job.rawData.assign(content.begin(), content.end());
    return job;
}

// ─── openDevice() tests ───────────────────────────────────────────────────────

TEST_CASE("PrinterManager::openDevice - success", "[manager]") {
    auto t = std::make_unique<MockUsbTransport>();
    PrinterManager mgr(std::move(t));

    UsbDeviceDescriptor dev;
    dev.vendorId  = 0x0A5F;
    dev.productId = 0x00D3;

    REQUIRE(mgr.openDevice(dev));
    REQUIRE(mgr.activeDevice().has_value());
    REQUIRE(mgr.activeDevice()->vendorId == 0x0A5F);
}

TEST_CASE("PrinterManager::openDevice - failure propagates", "[manager]") {
    MockConfig cfg;
    cfg.failOnOpen = true;
    auto t = std::make_unique<MockUsbTransport>(cfg);
    PrinterManager mgr(std::move(t));

    UsbDeviceDescriptor dev;
    dev.vendorId  = 0x1234;
    dev.productId = 0x5678;

    REQUIRE_FALSE(mgr.openDevice(dev));
    REQUIRE_FALSE(mgr.activeDevice().has_value());
}

// ─── submitJob() protocol auto-detection ─────────────────────────────────────

TEST_CASE("PrinterManager::submitJob - TSPL auto-detected via probe", "[manager][autodetect]") {
    auto t = std::make_unique<MockUsbTransport>();
    MockUsbTransport* raw = t.get();
    PrinterManager mgr(std::move(t));
    mgr.registerHandler(std::make_unique<TsplHandler>());

    UsbDeviceDescriptor dev;
    REQUIRE(mgr.openDevice(dev));

    auto job = makeTsplJob("SIZE 100 mm, 50 mm\r\nCLS\r\nPRINT 1,1\r\n");
    REQUIRE(mgr.submitJob(std::move(job)));
    REQUIRE_FALSE(raw->writtenBytes().empty());
}

TEST_CASE("PrinterManager::submitJob - no handler match returns false", "[manager][autodetect]") {
    auto t = std::make_unique<MockUsbTransport>();
    PrinterManager mgr(std::move(t));
    mgr.registerHandler(std::make_unique<TsplHandler>());

    UsbDeviceDescriptor dev;
    REQUIRE(mgr.openDevice(dev));

    // PCL data — TsplHandler won't match
    PrintJob job;
    job.jobId = "pcl";
    job.rawData = {0x1B, 'E', 0x1B, '&', 'l', '0', 'O'};

    REQUIRE_FALSE(mgr.submitJob(std::move(job)));
}

TEST_CASE("PrinterManager::submitJob - explicit TSPL hint bypasses probe", "[manager][hint]") {
    auto t = std::make_unique<MockUsbTransport>();
    MockUsbTransport* raw = t.get();
    PrinterManager mgr(std::move(t));
    mgr.registerHandler(std::make_unique<TsplHandler>());

    UsbDeviceDescriptor dev;
    REQUIRE(mgr.openDevice(dev));

    // Data that wouldn't probe as TSPL — forced by hint
    PrintJob job;
    job.jobId = "forced";
    job.rawData = {'H', 'E', 'L', 'L', 'O'};

    REQUIRE(mgr.submitJob(std::move(job), ProtocolType::TSPL));
    REQUIRE(raw->writtenBytes().size() == 5);
}

TEST_CASE("PrinterManager::submitJob - protocol cached after first detection", "[manager][cache]") {
    auto t = std::make_unique<MockUsbTransport>();
    PrinterManager mgr(std::move(t));
    mgr.registerHandler(std::make_unique<TsplHandler>());

    UsbDeviceDescriptor dev;
    REQUIRE(mgr.openDevice(dev));

    // First job — auto-detect TSPL
    auto job1 = makeTsplJob("SIZE 100 mm, 50 mm\r\nCLS\r\nPRINT 1,1\r\n");
    REQUIRE(mgr.submitJob(std::move(job1)));

    // Second job — same manager should use cached TSPL protocol
    // (even if the data doesn't match the probe pattern)
    PrintJob job2;
    job2.jobId = "second";
    job2.rawData = {'D', 'A', 'T', 'A'};

    REQUIRE(mgr.submitJob(std::move(job2)));
}

TEST_CASE("PrinterManager::submitJob - no device open returns false", "[manager]") {
    auto t = std::make_unique<MockUsbTransport>();
    PrinterManager mgr(std::move(t));
    mgr.registerHandler(std::make_unique<TsplHandler>());

    // Don't call openDevice
    auto job = makeTsplJob("SIZE 100 mm, 50 mm\r\nPRINT 1,1\r\n");
    REQUIRE_FALSE(mgr.submitJob(std::move(job)));
}

// ─── IEEE 1284 hint extraction ───────────────────────────────────────────────

TEST_CASE("PrinterManager - IEEE 1284 TSPL hint", "[manager][ieee1284]") {
    auto t = std::make_unique<MockUsbTransport>();
    MockUsbTransport* raw = t.get();
    PrinterManager mgr(std::move(t));
    mgr.registerHandler(std::make_unique<TsplHandler>());

    UsbDeviceDescriptor dev;
    dev.ieee1284Id = "MFG:Zebra;CMD:ZPL;MDL:ZD421;COMMAND SET:TSPL;";
    REQUIRE(mgr.openDevice(dev));

    // Data that wouldn't normally match TSPL probe
    PrintJob job;
    job.jobId = "ieee1284_tspl";
    job.rawData = {'T', 'E', 'S', 'T'};

    // IEEE 1284 hint routes to TSPL handler
    REQUIRE(mgr.submitJob(std::move(job)));
    REQUIRE(raw->writtenBytes().size() == 4);
}

TEST_CASE("PrinterManager - IEEE 1284 CMD:ZPL hint", "[manager][ieee1284]") {
    auto t = std::make_unique<MockUsbTransport>();
    MockUsbTransport* raw = t.get();
    PrinterManager mgr(std::move(t));
    mgr.registerHandler(std::make_unique<TsplHandler>());

    UsbDeviceDescriptor dev;
    dev.ieee1284Id = "MFG:Zebra;CMD:ZPL;MDL:ZD621;";
    REQUIRE(mgr.openDevice(dev));

    PrintJob job;
    job.jobId = "ieee1284_zpl";
    job.rawData = {'Z', 'P', 'L'};

    REQUIRE(mgr.submitJob(std::move(job)));
    REQUIRE(raw->writtenBytes().size() == 3);
}