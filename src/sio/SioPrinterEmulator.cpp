#include "sio/SioPrinterEmulator.h"
#include "sio/SioProtocol.h"
#include "util/Logger.h"

#ifdef PLATFORM_RP2040
#include "pico/time.h"
static uint32_t millis() {
    return to_ms_since_boot(get_absolute_time());
}
#else
#include <chrono>
static uint32_t millis() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}
#endif

static const char* TAG = "SioPrinterEmulator";

static const char* cmdName(uint8_t cmd) {
    switch (cmd) {
        case Sio::CMD_WRITE:  return "WRITE";
        case Sio::CMD_PUT:    return "PUT";
        case Sio::CMD_STATUS: return "STATUS";
        default:              return "?";
    }
}

SioPrinterEmulator::SioPrinterEmulator(ISioPort& port,
                                        ITextGenerator& generator,
                                        LineAssembler::Mode colMode,
                                        const TextConfig& defaultConfig)
    : m_port(port)
    , m_generator(generator)
    , m_assembler(colMode, generator, defaultConfig)
{}

uint32_t SioPrinterEmulator::nowMs() const {
    return millis();
}

void SioPrinterEmulator::reset() {
    m_state      = State::IDLE;
    m_pendingCmd = 0;
    m_lastAux1   = 0;
    m_lastCmdMs  = 0;
}

bool SioPrinterEmulator::recvBytes(uint8_t* buf, uint8_t count, uint32_t timeoutUs) {
    for (uint8_t i = 0; i < count; ++i) {
        if (!m_port.readByte(buf[i], timeoutUs)) {
            LOG_WARN(TAG, "timeout receiving byte %u/%u", i+1, count);
            return false;
        }
    }
    return true;
}

void SioPrinterEmulator::sendNak() {
    m_port.writeByte(Sio::NAK);
    LOG_DEBUG(TAG, "→ NAK");
}

void SioPrinterEmulator::sendAck() {
    m_port.writeByte(Sio::ACK);
    LOG_DEBUG(TAG, "→ ACK");
}

void SioPrinterEmulator::sendComplete() {
    m_port.delayUs(Sio::T_COMPLETE_US);
    m_port.writeByte(Sio::COMPLETE);
    LOG_DEBUG(TAG, "→ COMPLETE");
}

void SioPrinterEmulator::sendStatus() {
    // STATUS response per Atari 820 service manual and OS ROM manual:
    //   byte 0: done/error flag (0 = ok)
    //   byte 1: AUX1 from last WRITE/PUT command
    //   byte 2: write timeout (5 = constant per OS ROM manual)
    //   byte 3: unused (0)
    // Preceded by COMPLETE (T5 delay), followed by checksum.
    m_port.delayUs(Sio::T_COMPLETE_US);
    m_port.writeByte(Sio::COMPLETE);
    uint8_t status[4] = {0x00, m_lastAux1, 0x05, 0x00};
    uint8_t cs = Sio::checksum(status, 4);
    m_port.writeBytes(status, 4);
    m_port.writeByte(cs);
    LOG_DEBUG(TAG, "→ COMPLETE + STATUS (lastAux1=0x%02x)", m_lastAux1);
}

void SioPrinterEmulator::tick() {
    switch (m_state) {

    case State::IDLE:
        if (m_port.cmdLineAsserted()) {
            LOG_DEBUG(TAG, "CMD line asserted → RECV_CMD_FRAME");
            m_state = State::RECV_CMD_FRAME;
        }
        return;

    case State::RECV_CMD_FRAME: {
        uint8_t buf[5];
        if (!recvBytes(buf, 5, 10000 /*10 ms per byte*/)) {
            sendNak();
            m_state = State::IDLE;
            return;
        }

        SioCommandFrame frame{buf[0], buf[1], buf[2], buf[3], buf[4]};
        if (!frame.isValid()) {
            LOG_WARN(TAG, "Bad command frame checksum");
            sendNak();
            m_state = State::IDLE;
            return;
        }
        if (!frame.isPrinterDevice()) {
            LOG_DEBUG(TAG, "Not a printer device (id=0x%02x), ignoring", frame.deviceId);
            m_state = State::IDLE;
            return;
        }
        if (frame.command != Sio::CMD_WRITE &&
            frame.command != Sio::CMD_PUT   &&
            frame.command != Sio::CMD_STATUS) {
            LOG_WARN(TAG, "Unsupported command 0x%02x", frame.command);
            sendNak();
            m_state = State::IDLE;
            return;
        }

        LOG_INFO(TAG, "SIO dev=0x%02x cmd=%s aux1=0x%02x aux2=0x%02x",
                 frame.deviceId, cmdName(frame.command), frame.aux1, frame.aux2);

        m_lastCmdMs  = nowMs();
        m_pendingCmd = frame.command;
        // For WRITE/PUT, aux1 determines record length; cache it now.
        if (frame.command != Sio::CMD_STATUS)
            m_lastAux1 = frame.aux1;

        m_state = State::SEND_ACK_CMD;
        break;
    }

    case State::SEND_ACK_CMD:
        m_port.delayUs(Sio::T_ACK_US);
        sendAck();
        m_state = (m_pendingCmd == Sio::CMD_STATUS)
                  ? State::SEND_STATUS
                  : State::RECV_RECORD;
        break;

    case State::RECV_RECORD: {
        // AUX1 determines record length: 'N'/default=40, 'S'=29, 'D'=20
        uint8_t recLen = Sio::recordLength(m_lastAux1);

        // Read recLen data bytes + 1 checksum byte
        if (!recvBytes(m_recordBuf, recLen + 1, 20000 /*20 ms*/)) {
            sendNak();
            m_state = State::IDLE;
            return;
        }

        uint8_t expected = Sio::checksum(m_recordBuf, recLen);
        if (expected != m_recordBuf[recLen]) {
            LOG_WARN(TAG, "Bad record checksum (got 0x%02x expected 0x%02x)",
                     m_recordBuf[recLen], expected);
            sendNak();
            m_state = State::IDLE;
            return;
        }

        // Echo data payload as printable ASCII for UART diagnostics.
        char printable[Sio::RECORD_SIZE + 1];
        for (uint8_t i = 0; i < recLen; ++i)
            printable[i] = (m_recordBuf[i] >= 0x20 && m_recordBuf[i] < 0x7F)
                           ? static_cast<char>(m_recordBuf[i]) : '.';
        printable[recLen] = '\0';
        LOG_INFO(TAG, "SIO data: \"%s\"", printable);

        m_state = State::SEND_ACK_DATA;
        break;
    }

    case State::SEND_ACK_DATA:
        sendAck();
        m_state = State::PROCESS_RECORD;
        break;

    case State::PROCESS_RECORD:
        m_assembler.ingest(m_recordBuf, Sio::recordLength(m_lastAux1));
        m_state = State::SEND_COMPLETE;
        break;

    case State::SEND_COMPLETE:
        sendComplete();
        m_state = State::IDLE;
        break;

    case State::SEND_STATUS:
        sendStatus();
        m_state = State::IDLE;
        break;
    }
}
