#pragma once
#include "sio/SioCommandFrame.h"
#include "sio/LineAssembler.h"
#include "generator/ITextGenerator.h"
#include "generator/TextConfig.h"
#include <cstdint>
#include <cstddef>

// Abstract byte-level I/O interface for SIO bus communication.
// Implemented by SioUart on RP2040; by MockSioPort in unit tests.
class ISioPort {
public:
    virtual ~ISioPort() = default;
    virtual bool cmdLineAsserted() const = 0;
    virtual bool readByte(uint8_t& out, uint32_t timeoutUs) = 0;
    virtual void writeByte(uint8_t b) = 0;
    virtual void writeBytes(const uint8_t* data, size_t len) = 0;
    virtual void delayUs(uint32_t us) = 0;
};

// SIO printer emulator state machine.
// Call tick() from the main loop. It is non-blocking.
// Received lines are forwarded to the ITextGenerator immediately.
// Job termination is caller-driven (e.g. GPIO button): call generator.flush()
// and submit the resulting PrintJob to PrinterManager.
class SioPrinterEmulator {
public:
    // defaultConfig: passed to LineAssembler as the baseline that ESC ~ R 0
    // resets to. Omitting it uses TextConfig{} — existing call sites unchanged.
    SioPrinterEmulator(ISioPort& port,
                       ITextGenerator& generator,
                       LineAssembler::Mode colMode,
                       const TextConfig& defaultConfig = TextConfig{});

    // Advance the state machine. Call as fast as possible from main loop.
    void tick();

    // Reset to IDLE state.
    void reset();

    // Millisecond timestamp of the last valid command frame received.
    // Returns 0 if no command has been seen since construction/reset.
    // Use to gate actions that must not interrupt SIO bus activity.
    uint32_t lastActivityMs() const { return m_lastCmdMs; }

    // Returns true (once) if an ESC ~ P sequence was received since last call.
    // Poll this after tick() and flush/submit the generator if true.
    bool takePrintRequest() { return m_assembler.takePrintRequest(); }

    // Returns true (once) if ESC ~ S was received; sets slot (0–9).
    bool takeSaveRequest(uint8_t& slot) { return m_assembler.takeSaveRequest(slot); }

    // Returns true (once) if ESC ~ R was received; sets slot (0–9).
    bool takeLoadRequest(uint8_t& slot) { return m_assembler.takeLoadRequest(slot); }

    // Apply an externally-loaded TextConfig (e.g. from flash).
    void setConfig(const TextConfig& cfg) { m_assembler.setConfig(cfg); }

    // Read current config (e.g. to pass to FlashConfig::save).
    const TextConfig& getConfig() const { return m_assembler.getConfig(); }

private:
    enum class State {
        IDLE,
        RECV_CMD_FRAME,
        SEND_ACK_CMD,
        RECV_RECORD,
        SEND_ACK_DATA,
        PROCESS_RECORD,
        SEND_COMPLETE,
        SEND_STATUS,        // ACK sent; now send COMPLETE + status frame
    };

    bool recvBytes(uint8_t* buf, uint8_t count, uint32_t timeoutUs);
    void sendNak();
    void sendAck();
    void sendComplete();
    void sendStatus();
    uint32_t nowMs() const;

    ISioPort&       m_port;
    ITextGenerator& m_generator;
    LineAssembler   m_assembler;

    State    m_state       = State::IDLE;
    uint8_t  m_pendingCmd  = 0;                // command byte from current frame
    uint8_t  m_lastAux1    = 0;                // AUX1 from last WRITE/PUT (for STATUS)
    uint8_t  m_recordBuf[Sio::RECORD_SIZE + 1]; // +1 for checksum byte
    uint32_t m_lastCmdMs   = 0;                // timestamp of last valid command frame
};
