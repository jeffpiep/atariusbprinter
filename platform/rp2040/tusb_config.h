#pragma once

#define CFG_TUSB_MCU  OPT_MCU_RP2040
#define CFG_TUSB_OS   OPT_OS_PICO

#define CFG_TUH_ENABLED  1   // USB host role
#define CFG_TUD_ENABLED  0   // no device/peripheral role

// No class drivers — raw endpoint transfers via tuh_edpt_xfer()
#define CFG_TUH_HID 0
#define CFG_TUH_MSC 0
#define CFG_TUH_CDC 0

// Enable raw endpoint transfer API (tuh_edpt_open + tuh_edpt_xfer)
// Required for tuh_edpt_xfer() to be dispatched by the host pipeline.
#define CFG_TUH_API_EDPT_XFER 1

#define CFG_TUH_ENUMERATION_BUFSIZE 512  // config descriptor fetch buffer
#define CFG_TUH_DEVICE_MAX          4    // max simultaneously tracked devices
