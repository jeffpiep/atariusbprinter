#pragma once
#include "transport/IUsbTransport.h"
#include "transport/IHotplugCapable.h"
#include "protocol/IProtocolHandler.h"
#include "protocol/PrintJob.h"
#include "protocol/ProtocolType.h"
#include "manager/DeviceRecord.h"
#include <memory>
#include <vector>
#include <optional>
#include <functional>

#ifndef PLATFORM_RP2040
#include <mutex>
#endif

class PrinterManager {
public:
    explicit PrinterManager(std::unique_ptr<IUsbTransport> transport);
    ~PrinterManager();

    // Register a protocol handler. Ownership transfers to manager.
    // Handlers are probed in registration order (first match wins).
    void registerHandler(std::unique_ptr<IProtocolHandler> handler);

    // Open a specific device directly (Phase 1 and Phase 2 manual path).
    bool openDevice(const UsbDeviceDescriptor& dev);

    // Begin hot-plug monitoring. On Linux: starts libusb hotplug thread.
    // On RP2040: no-op (tick() polls from main loop).
    void start();
    void stop();

    // RP2040 main-loop polling tick. No-op on Linux (hotplug is threaded).
    void tick();

    // Synchronously submit a job. Performs protocol auto-detection if
    // hint == ProtocolType::UNKNOWN.
    bool submitJob(PrintJob job, ProtocolType hint = ProtocolType::UNKNOWN);

    // Returns the currently active device, or nullopt if none.
    std::optional<UsbDeviceDescriptor> activeDevice() const;

    // Callback invoked on attach/detach. Set before calling start().
    std::function<void(const UsbDeviceDescriptor&, bool /*attached*/)>
        onDeviceChange;

    // Hotplug entry points — public for unit testing.
    void onAttach(const UsbDeviceDescriptor& desc);
    void onDetach(uint8_t busNumber, uint8_t deviceAddress);

private:
    IProtocolHandler* detectProtocol(const PrintJob& job, ProtocolType hint,
                                     DeviceRecord& record);
    IProtocolHandler* findHandlerByType(ProtocolType proto);
    DeviceRecord*     findActiveDevice();   // caller must hold m_mutex

    std::unique_ptr<IUsbTransport>                 m_transport;
    std::vector<std::unique_ptr<IProtocolHandler>> m_handlers;
    std::vector<DeviceRecord>                      m_devices;

    // Which device the transport is currently open to
    uint8_t m_openBus  = 0;
    uint8_t m_openAddr = 0;
    bool    m_transportBound = false;

#ifndef PLATFORM_RP2040
    mutable std::mutex m_mutex;
#endif
};
