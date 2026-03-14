#include "manager/PrinterManager.h"
#include "util/Logger.h"
#include <cstring>

static const char* TAG = "PrinterManager";

// ── Construction ──────────────────────────────────────────────────────────────

PrinterManager::PrinterManager(std::unique_ptr<IUsbTransport> transport)
    : m_transport(std::move(transport)) {}

PrinterManager::~PrinterManager() {
    stop();
    if (m_transport && m_transport->isOpen()) m_transport->close();
}

// ── Registration ──────────────────────────────────────────────────────────────

void PrinterManager::registerHandler(std::unique_ptr<IProtocolHandler> handler) {
    LOG_INFO(TAG, "Registered handler: %s", handler->name());
    m_handlers.push_back(std::move(handler));
}

// ── Device management ─────────────────────────────────────────────────────────

bool PrinterManager::openDevice(const UsbDeviceDescriptor& dev) {
    if (!m_transport) return false;

#ifndef PLATFORM_RP2040
    std::lock_guard<std::mutex> lk(m_mutex);
#endif

    if (m_transport->isOpen()) m_transport->close();

    if (!m_transport->open(dev)) {
        LOG_ERROR(TAG, "Failed to open device %s", dev.description().c_str());
        return false;
    }

    // Register in device registry if not already present
    bool found = false;
    for (auto& r : m_devices) {
        if (r.descriptor.busNumber == dev.busNumber &&
            r.descriptor.deviceAddress == dev.deviceAddress) {
            r.active = true;
            found = true;
            break;
        }
    }
    if (!found) {
        DeviceRecord rec;
        rec.descriptor = dev;
        rec.active     = true;
        m_devices.push_back(rec);
    }

    m_openBus       = dev.busNumber;
    m_openAddr      = dev.deviceAddress;
    m_transportBound = true;

    LOG_INFO(TAG, "Device opened: %s", dev.description().c_str());
    return true;
}

void PrinterManager::onAttach(const UsbDeviceDescriptor& desc) {
#ifndef PLATFORM_RP2040
    std::lock_guard<std::mutex> lk(m_mutex);
#endif

    for (auto& r : m_devices) {
        if (r.descriptor.busNumber == desc.busNumber &&
            r.descriptor.deviceAddress == desc.deviceAddress) {
            r.descriptor = desc;
            r.active     = true;
            LOG_INFO(TAG, "Device re-attached: %s", desc.description().c_str());
            if (onDeviceChange) onDeviceChange(desc, true);
            return;
        }
    }

    DeviceRecord rec;
    rec.descriptor = desc;
    rec.active     = true;
    m_devices.push_back(rec);

    LOG_INFO(TAG, "Device attached: %s", desc.description().c_str());
    if (onDeviceChange) onDeviceChange(desc, true);
}

void PrinterManager::onDetach(uint8_t bus, uint8_t addr) {
#ifndef PLATFORM_RP2040
    std::lock_guard<std::mutex> lk(m_mutex);
#endif

    for (auto& r : m_devices) {
        if (r.descriptor.busNumber == bus && r.descriptor.deviceAddress == addr) {
            r.active = false;
            r.detectedProtocol = ProtocolType::UNKNOWN; // reset cached protocol

            if (m_transportBound && m_openBus == bus && m_openAddr == addr) {
                m_transport->close();
                m_transportBound = false;
                LOG_INFO(TAG, "Transport closed (device detached)");
            }

            LOG_INFO(TAG, "Device detached: %s", r.descriptor.description().c_str());
            if (onDeviceChange) onDeviceChange(r.descriptor, false);
            return;
        }
    }
    LOG_WARN(TAG, "onDetach: unknown device bus%u addr%u", bus, addr);
}

DeviceRecord* PrinterManager::findActiveDevice() {
    for (auto& r : m_devices) {
        if (r.active) return &r;
    }
    return nullptr;
}

// ── Hot-plug ──────────────────────────────────────────────────────────────────

void PrinterManager::start() {
#ifndef PLATFORM_RP2040
    auto* hp = dynamic_cast<IHotplugCapable*>(m_transport.get());
    if (!hp) return;

    hp->startHotplug([this](UsbDeviceDescriptor desc, bool attached) {
        if (attached) onAttach(desc);
        else          onDetach(desc.busNumber, desc.deviceAddress);
    });
#endif
}

void PrinterManager::stop() {
#ifndef PLATFORM_RP2040
    auto* hp = dynamic_cast<IHotplugCapable*>(m_transport.get());
    if (hp) hp->stopHotplug();
#endif
}

void PrinterManager::tick() {
    // RP2040: hotplug is polled via tuh_task() in main loop, nothing needed here.
}

// ── Protocol detection ────────────────────────────────────────────────────────

IProtocolHandler* PrinterManager::findHandlerByType(ProtocolType proto) {
    for (auto& h : m_handlers) {
        const char* n = h->name();
        if (proto == ProtocolType::TSPL   && n[0]=='T') return h.get();
        if (proto == ProtocolType::PCL    && n[0]=='P') return h.get();
        if (proto == ProtocolType::ESCPOS && strcmp(n, "ESCPOS") == 0) return h.get();
    }
    return nullptr;
}

IProtocolHandler* PrinterManager::detectProtocol(const PrintJob& job,
                                                  ProtocolType hint,
                                                  DeviceRecord& record) {
    // 1. IEEE 1284 device ID hint
    if (hint == ProtocolType::UNKNOWN && !record.descriptor.ieee1284Id.empty()) {
        const std::string& id = record.descriptor.ieee1284Id;
        if (id.find("COMMAND SET:TSPL") != std::string::npos ||
            id.find("CMD:ZPL")         != std::string::npos)
            hint = ProtocolType::TSPL;
        else if (id.find("COMMAND SET:PCL") != std::string::npos)
            hint = ProtocolType::PCL;
    }

    // 2. Cached protocol from previous job
    if (hint == ProtocolType::UNKNOWN &&
        record.detectedProtocol != ProtocolType::UNKNOWN)
        hint = record.detectedProtocol;

    // 3. Direct lookup by hint
    if (hint != ProtocolType::UNKNOWN) {
        IProtocolHandler* h = findHandlerByType(hint);
        if (h) return h;
    }

    // 4. Probe each handler in registration order
    const uint8_t* peekData = job.rawData.empty() ? nullptr : job.rawData.data();
    size_t peekLen = job.rawData.size() < 64 ? job.rawData.size() : 64;
    for (auto& h : m_handlers) {
        if (peekData && h->probe(peekData, peekLen)) return h.get();
    }
    return nullptr;
}

// ── Job dispatch ──────────────────────────────────────────────────────────────

bool PrinterManager::submitJob(PrintJob job, ProtocolType hint) {
#ifndef PLATFORM_RP2040
    std::unique_lock<std::mutex> lk(m_mutex);
#endif

    if (!m_transport) {
        LOG_ERROR(TAG, "submitJob: no transport");
        return false;
    }

    DeviceRecord* record = findActiveDevice();
    if (!record) {
        LOG_ERROR(TAG, "submitJob: no active device");
        return false;
    }

    // Open transport to this device if not already open to it
    if (!m_transportBound ||
        m_openBus  != record->descriptor.busNumber ||
        m_openAddr != record->descriptor.deviceAddress) {

        if (m_transport->isOpen()) m_transport->close();

        if (!m_transport->open(record->descriptor)) {
            LOG_ERROR(TAG, "submitJob: failed to open %s",
                      record->descriptor.description().c_str());
            return false;
        }
        m_openBus        = record->descriptor.busNumber;
        m_openAddr       = record->descriptor.deviceAddress;
        m_transportBound = true;
    }

    IProtocolHandler* handler = detectProtocol(job, hint, *record);
    if (!handler) {
        LOG_ERROR(TAG, "submitJob '%s': no matching protocol handler",
                  job.jobId.c_str());
        return false;
    }

    // Cache detected protocol
    if (record->detectedProtocol == ProtocolType::UNKNOWN) {
        LOG_INFO(TAG, "Protocol auto-detected: %s", handler->name());
        const char* n = handler->name();
        if      (n[0]=='T')                    record->detectedProtocol = ProtocolType::TSPL;
        else if (n[0]=='P')                    record->detectedProtocol = ProtocolType::PCL;
        else if (strcmp(n, "ESCPOS") == 0)     record->detectedProtocol = ProtocolType::ESCPOS;
    }

#ifndef PLATFORM_RP2040
    lk.unlock(); // release lock before potentially long write
#endif

    return handler->sendJob(*m_transport, job);
}

std::optional<UsbDeviceDescriptor> PrinterManager::activeDevice() const {
#ifndef PLATFORM_RP2040
    std::lock_guard<std::mutex> lk(m_mutex);
#endif
    for (const auto& r : m_devices) {
        if (r.active) return r.descriptor;
    }
    return std::nullopt;
}
