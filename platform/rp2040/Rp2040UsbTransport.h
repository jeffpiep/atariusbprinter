#pragma once
#ifdef PLATFORM_RP2040
#include "transport/IUsbTransport.h"
#include "tusb.h"
#include <cstdint>

// Maximum number of simultaneously tracked USB printer devices.
static constexpr int RP2040_MAX_DEVICES = 4;

// Static buffer size for bulk transfers (USB Full Speed = 64, High Speed = 512).
static constexpr uint16_t RP2040_MAX_PACKET = 64;

class Rp2040UsbTransport : public IUsbTransport {
public:
    Rp2040UsbTransport() = default;

    bool open(const UsbDeviceDescriptor& dev) override;
    void close() override;
    int  write(const uint8_t* data, size_t len,
               uint32_t timeout_ms = 2000) override;
    int  read(uint8_t* buf, size_t maxLen,
              uint32_t timeout_ms = 500) override;
    bool isOpen() const override;

    // Called from TinyUSB mount/unmount callbacks (registered in main_rp2040.cpp).
    static void onMount(uint8_t devAddr, uint8_t interfaceNum,
                        uint8_t epOut, uint8_t epIn, bool hasEpIn);
    static void onUnmount(uint8_t devAddr);

    // Non-blocking async write API for use in cooperative main loops.
    // beginWrite() queues one chunk (≤ RP2040_MAX_PACKET bytes).
    // Call pollWrite() each iteration until isBusy() returns false.
    bool beginWrite(const uint8_t* data, size_t len);
    bool pollWrite();  // pumps tuh_task(); returns true when transfer done
    bool isBusy() const { return m_open && !m_txDone; }

private:
    // Transfer completion callback (static, registered per transfer).
    static void transferCallback(tuh_xfer_t* xfer);

    struct DevEntry {
        uint8_t devAddr    = 0;
        uint8_t epOut      = 0;
        uint8_t epIn       = 0;
        bool    hasEpIn    = false;
        bool    active     = false;
    };

    static DevEntry s_devices[RP2040_MAX_DEVICES];

    uint8_t m_devAddr  = 0;
    uint8_t m_epOut    = 0;
    uint8_t m_epIn     = 0;
    bool    m_hasEpIn  = false;
    bool    m_open     = false;

    // Transfer completion signalling (polled, no RTOS semaphore needed)
    volatile bool    m_txDone    = false;
    volatile int32_t m_txResult  = 0;
    static uint8_t   s_txBuf[RP2040_MAX_PACKET];
    static uint8_t   s_rxBuf[RP2040_MAX_PACKET];
};
#endif // PLATFORM_RP2040
