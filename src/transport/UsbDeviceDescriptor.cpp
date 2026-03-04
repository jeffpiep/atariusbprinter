#include "transport/UsbDeviceDescriptor.h"
#include <cstdio>

std::string UsbDeviceDescriptor::description() const {
    char buf[128];
    snprintf(buf, sizeof(buf), "%04x:%04x bus%u addr%u \"%s %s\"",
             vendorId, productId, busNumber, deviceAddress,
             manufacturer.c_str(), product.c_str());
    return buf;
}
