#pragma once
#include "transport/IUsbTransport.h"
#include "transport/IHotplugCapable.h"
#include <libusb-1.0/libusb.h>
#include <atomic>
#include <thread>

enum class TransportError {
    None,
    Timeout,
    Disconnected,
    AccessDenied,
    Unknown
};

class LinuxUsbTransport : public IUsbTransport, public IHotplugCapable {
public:
    LinuxUsbTransport();
    ~LinuxUsbTransport() override;

    LinuxUsbTransport(const LinuxUsbTransport&) = delete;
    LinuxUsbTransport& operator=(const LinuxUsbTransport&) = delete;

    // IUsbTransport
    bool open(const UsbDeviceDescriptor& dev) override;
    void close() override;
    int  write(const uint8_t* data, size_t len,
               uint32_t timeout_ms = 2000) override;
    int  read(uint8_t* buf, size_t maxLen,
              uint32_t timeout_ms = 500) override;
    bool isOpen() const override;

    // IHotplugCapable
    bool startHotplug(HotplugCb cb) override;
    void stopHotplug() override;

    // Enumerate all connected printer-class USB devices.
    static int enumeratePrinters(UsbDeviceDescriptor* out, int maxCount);

    TransportError lastError() const { return m_lastError; }

private:
    static TransportError mapLibusbError(int err);
    bool claimPrinterInterface();
    void releaseInterface();

    // Hotplug callback (libusb calls this from the event thread)
    static int LIBUSB_CALL hotplugCallback(libusb_context* ctx,
                                           libusb_device* dev,
                                           libusb_hotplug_event event,
                                           void* userData);
    static void fillDescriptor(libusb_device* dev,
                               const libusb_device_descriptor& ddesc,
                               UsbDeviceDescriptor& out);

    libusb_context*              m_ctx             = nullptr;
    libusb_device_handle*        m_handle          = nullptr;
    int                          m_interfaceNum    = -1;
    uint8_t                      m_epOut           = 0;
    uint8_t                      m_epIn            = 0;
    bool                         m_hasEpIn         = false;
    TransportError               m_lastError       = TransportError::None;

    // Hotplug
    HotplugCb                          m_hotplugCb;
    libusb_hotplug_callback_handle     m_hotplugHandle  = 0;
    std::thread                        m_eventThread;
    std::atomic<bool>                  m_eventRunning{false};
};
