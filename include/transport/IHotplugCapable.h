#pragma once
#include "transport/UsbDeviceDescriptor.h"
#include <functional>

// Optional interface implemented by transports that support hot-plug detection.
// PrinterManager uses dynamic_cast to check if the transport implements this.
class IHotplugCapable {
public:
    using HotplugCb = std::function<void(UsbDeviceDescriptor, bool /*attached*/)>;

    virtual ~IHotplugCapable() = default;

    // Start hot-plug monitoring. Calls cb on the monitoring thread (Linux) or
    // from tick() (RP2040). Returns false if hotplug is not supported by the OS.
    virtual bool startHotplug(HotplugCb cb) = 0;

    // Stop hot-plug monitoring and join the monitoring thread if applicable.
    virtual void stopHotplug() = 0;
};
