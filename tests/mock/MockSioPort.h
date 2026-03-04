#pragma once
#include "sio/SioPrinterEmulator.h"
#include <deque>
#include <vector>
#include <cstdint>

// Configurable mock for ISioPort, used in SioPrinterEmulator unit tests.
class MockSioPort : public ISioPort {
public:
    // Bytes queued for readByte()
    std::deque<uint8_t> rxQueue;

    // Bytes captured from writeByte() / writeBytes()
    std::vector<uint8_t> txLog;

    // If true, cmdLineAsserted() returns true once then resets
    bool cmdAsserted = false;

    // If true, readByte() always times out (returns false)
    bool alwaysTimeout = false;

    // Accumulated delay microseconds
    uint32_t totalDelayUs = 0;

    // ── ISioPort ─────────────────────────────────────────────────────────────

    bool cmdLineAsserted() const override {
        if (cmdAsserted) {
            const_cast<MockSioPort*>(this)->cmdAsserted = false;
            return true;
        }
        return false;
    }

    bool readByte(uint8_t& out, uint32_t /*timeoutUs*/) override {
        if (alwaysTimeout || rxQueue.empty()) return false;
        out = rxQueue.front();
        rxQueue.pop_front();
        return true;
    }

    void writeByte(uint8_t b) override {
        txLog.push_back(b);
    }

    void writeBytes(const uint8_t* data, size_t len) override {
        for (size_t i = 0; i < len; ++i)
            txLog.push_back(data[i]);
    }

    void delayUs(uint32_t us) override {
        totalDelayUs += us;
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Queue raw bytes for the emulator to read
    void enqueue(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) rxQueue.push_back(data[i]);
    }

    void enqueue(std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) rxQueue.push_back(b);
    }

    void clearTx() { txLog.clear(); }
};
