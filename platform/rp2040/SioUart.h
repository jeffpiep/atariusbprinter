#pragma once
#ifdef PLATFORM_RP2040
#include "sio/SioPrinterEmulator.h"
#include "sio/SioProtocol.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <cstdint>
#include <atomic>

// RP2040 UART + GPIO implementation of ISioPort.
// Owns UART1 (default) and the SIO command-line GPIO.
class SioUart : public ISioPort {
public:
    struct Config {
        uart_inst_t* uart     = uart1;
        uint         txPin    = 4;
        uint         rxPin    = 5;
        uint         cmdPin   = Sio::CMD_LINE_GPIO; // GPIO 26
        uint32_t     baudRate = Sio::BAUD_RATE;
    };

    explicit SioUart(const Config& cfg);

    void init();

    // ISioPort
    bool cmdLineAsserted() const override;
    bool readByte(uint8_t& out, uint32_t timeoutUs) override;
    void writeByte(uint8_t b) override;
    void writeBytes(const uint8_t* data, size_t len) override;
    void flushTx();
    void delayUs(uint32_t us) override;

private:
    static void gpioIrqHandler(uint gpio, uint32_t events);

    Config m_cfg;
    static std::atomic<bool> s_cmdAsserted;
};
#endif // PLATFORM_RP2040
