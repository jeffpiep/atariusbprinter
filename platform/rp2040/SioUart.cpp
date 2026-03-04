#ifdef PLATFORM_RP2040
#include "SioUart.h"
#include "util/Logger.h"

static const char* TAG = "SioUart";

std::atomic<bool> SioUart::s_cmdAsserted{false};

SioUart::SioUart(const Config& cfg) : m_cfg(cfg) {}

void SioUart::init() {
    uart_init(m_cfg.uart, m_cfg.baudRate);
    gpio_set_function(m_cfg.txPin, GPIO_FUNC_UART);
    gpio_set_function(m_cfg.rxPin, GPIO_FUNC_UART);
    uart_set_format(m_cfg.uart, 8, 1, UART_PARITY_NONE);

    // Command line GPIO: input with pull-up; active-low
    gpio_init(m_cfg.cmdPin);
    gpio_set_dir(m_cfg.cmdPin, GPIO_IN);
    gpio_pull_up(m_cfg.cmdPin);

    // Interrupt on falling edge (command line asserted)
    gpio_set_irq_enabled_with_callback(m_cfg.cmdPin,
                                        GPIO_IRQ_EDGE_FALL,
                                        true,
                                        &SioUart::gpioIrqHandler);

    LOG_INFO(TAG, "SIO UART init: baud=%u tx=%u rx=%u cmd=%u",
             m_cfg.baudRate, m_cfg.txPin, m_cfg.rxPin, m_cfg.cmdPin);
}

// static
void SioUart::gpioIrqHandler(uint gpio, uint32_t events) {
    (void)gpio; (void)events;
    s_cmdAsserted.store(true, std::memory_order_relaxed);
}

bool SioUart::cmdLineAsserted() const {
    // Rising edge on GPIO means command line de-asserted; poll current level too
    bool val = !gpio_get(m_cfg.cmdPin); // active-low
    if (val) s_cmdAsserted.store(true, std::memory_order_relaxed);
    bool asserted = s_cmdAsserted.exchange(false, std::memory_order_relaxed);
    return asserted || val;
}

bool SioUart::readByte(uint8_t& out, uint32_t timeoutUs) {
    uint64_t deadline = time_us_64() + timeoutUs;
    while (!uart_is_readable(m_cfg.uart)) {
        if (time_us_64() >= deadline) return false;
        tight_loop_contents();
    }
    out = uart_getc(m_cfg.uart);
    return true;
}

void SioUart::writeByte(uint8_t b) {
    uart_putc_raw(m_cfg.uart, static_cast<char>(b));
}

void SioUart::writeBytes(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        uart_putc_raw(m_cfg.uart, static_cast<char>(data[i]));
    }
}

void SioUart::flushTx() {
    uart_tx_wait_blocking(m_cfg.uart);
}

void SioUart::delayUs(uint32_t us) {
    sleep_us(us);
}

#endif // PLATFORM_RP2040
