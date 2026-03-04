#pragma once
#include "sio/SioCommandFrame.h"
#include "sio/LineAssembler.h"
#include "generator/ITextGenerator.h"
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
    SioPrinterEmulator(ISioPort& port,
                       ITextGenerator& generator,
                       LineAssembler::Mode colMode);

    // Advance the state machine. Call as fast as possible from main loop.
    void tick();

    // Reset to IDLE state.
    void reset();

    // Millisecond timestamp of the last valid command frame received.
    // Returns 0 if no command has been seen since construction/reset.
    // Use to gate actions that must not interrupt SIO bus activity.
    uint32_t lastActivityMs() const { return m_lastCmdMs; }

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
