# USB Printer Driver — Software Specification
**Project:** Multi-Protocol USB Printer Driver (TSPL / PCL / ESC/P)  
**Targets:** Linux (x86_64 / ARM), RP2040 Microcontroller  
**Language:** C++17  
**Toolchain:** CMake ≥ 3.20, GCC/Clang (Linux), pico-sdk (RP2040)  
**Revision:** 1.0 — 2026-02-25

---

## 1. Overview

This document specifies a three-phase, cross-platform USB printer driver library and host application supporting TSPL (ZPL-compatible label printers), simple PCL (laser/inkjet), and ESC/P (dot-matrix/receipt) protocols. The architecture is designed so that protocol handling, transport (USB), and platform specifics are cleanly separated to allow the same core logic to run on both a full Linux host and a resource-constrained RP2040 microcontroller acting as a USB host bridge.

---

## 2. High-Level Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        Application Layer                      │
│          (CLI tool on Linux / embedded main on RP2040)        │
└───────────────────────────┬──────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────┐
│                     PrinterManager                            │
│   Hot-plug detection · device registry · job dispatch        │
└───┬───────────────────────┬──────────────────────────────────┘
    │                       │
┌───▼──────────┐   ┌────────▼──────────────────────────────────┐
│ UsbTransport │   │           Protocol Handler Registry        │
│  (abstract)  │   │  ┌─────────────┐ ┌───────┐ ┌──────────┐  │
│              │   │  │ TsplHandler │ │PclHdlr│ │EscPHandler│ │
│  LinuxUsb    │   │  └─────────────┘ └───────┘ └──────────┘  │
│  Rp2040Usb   │   └───────────────────────────────────────────┘
└──────────────┘
```

All inter-layer communication uses abstract C++ interfaces. Platform-specific code lives only in the `platform/` subtree and is selected at compile time via CMake target definitions.

---

## 3. Repository Layout

```
usb_printer_driver/
├── CMakeLists.txt               # Top-level; selects platform target
├── cmake/
│   ├── platform_linux.cmake
│   └── platform_rp2040.cmake
├── include/
│   ├── transport/
│   │   ├── IUsbTransport.h      # Abstract USB transport interface
│   │   └── UsbDeviceDescriptor.h
│   ├── protocol/
│   │   ├── IProtocolHandler.h   # Abstract protocol handler interface
│   │   ├── ProtocolType.h       # Enum: TSPL, PCL, ESCP, UNKNOWN
│   │   └── PrintJob.h           # Language-agnostic job container
│   ├── manager/
│   │   ├── PrinterManager.h
│   │   └── DeviceRecord.h
│   └── util/
│       ├── Logger.h
│       └── RingBuffer.h
├── src/
│   ├── transport/
│   │   └── UsbDeviceDescriptor.cpp
│   ├── protocol/
│   │   ├── TsplHandler.cpp      # Phase 1+
│   │   ├── PclHandler.cpp       # Phase 3
│   │   └── EscpHandler.cpp      # Phase 3
│   ├── manager/
│   │   └── PrinterManager.cpp
│   └── util/
│       ├── Logger.cpp
│       └── RingBuffer.cpp
├── platform/
│   ├── linux/
│   │   ├── LinuxUsbTransport.h
│   │   ├── LinuxUsbTransport.cpp   # libusb-1.0 backend
│   │   └── main_linux.cpp
│   └── rp2040/
│       ├── Rp2040UsbTransport.h
│       ├── Rp2040UsbTransport.cpp  # TinyUSB host backend
│       └── main_rp2040.cpp
├── tests/
│   ├── test_tspl_handler.cpp
│   ├── test_pcl_handler.cpp
│   ├── test_escp_handler.cpp
│   ├── test_printer_manager.cpp
│   └── mock/
│       ├── MockUsbTransport.h
│       └── MockUsbTransport.cpp
└── docs/
    └── protocol_notes/
        ├── tspl_reference.md
        ├── pcl_subset.md
        └── escp_subset.md
```

---

## 4. Core Interfaces

### 4.1 `IUsbTransport`

```cpp
// include/transport/IUsbTransport.h
class IUsbTransport {
public:
    virtual ~IUsbTransport() = default;

    // Open a specific device. Returns false on failure.
    virtual bool open(const UsbDeviceDescriptor& dev) = 0;

    // Close the currently open device.
    virtual void close() = 0;

    // Blocking bulk write to the printer OUT endpoint.
    // Returns number of bytes written, or -1 on error.
    virtual int write(const uint8_t* data, size_t len,
                      uint32_t timeout_ms = 2000) = 0;

    // Blocking bulk read from the printer IN endpoint (status).
    // Returns number of bytes read, or -1 on error.
    virtual int read(uint8_t* buf, size_t maxLen,
                     uint32_t timeout_ms = 500) = 0;

    // Query device open state.
    virtual bool isOpen() const = 0;
};
```

### 4.2 `UsbDeviceDescriptor`

```cpp
// include/transport/UsbDeviceDescriptor.h
struct UsbDeviceDescriptor {
    uint16_t    vendorId;
    uint16_t    productId;
    std::string manufacturer;
    std::string product;
    std::string serialNumber;
    uint8_t     busNumber;
    uint8_t     deviceAddress;
    // IEEE 1284 device ID string, if available (used for protocol detection)
    std::string ieee1284Id;
};
```

### 4.3 `IProtocolHandler`

```cpp
// include/protocol/IProtocolHandler.h
class IProtocolHandler {
public:
    virtual ~IProtocolHandler() = default;

    // Validate that the supplied data stream begins with a recognized
    // command prefix for this protocol. Used during auto-detection.
    virtual bool probe(const uint8_t* data, size_t len) const = 0;

    // Process and forward a complete print job to the transport.
    // Returns true on success.
    virtual bool sendJob(IUsbTransport& transport,
                         const PrintJob& job) = 0;

    // Query the printer status. Populates statusOut; returns true if
    // a valid status response was received.
    virtual bool queryStatus(IUsbTransport& transport,
                             std::string& statusOut) = 0;

    // Human-readable protocol name for logging.
    virtual const char* name() const = 0;
};
```

### 4.4 `PrintJob`

```cpp
// include/protocol/PrintJob.h
struct PrintJob {
    std::vector<uint8_t> rawData;   // Pre-formed command stream
    std::string          jobId;     // Opaque identifier for logging
    uint32_t             timeoutMs; // Per-job write timeout
};
```

### 4.5 `PrinterManager`

```cpp
// include/manager/PrinterManager.h
class PrinterManager {
public:
    explicit PrinterManager(std::unique_ptr<IUsbTransport> transport);

    // Register a protocol handler. Ownership transfers to manager.
    void registerHandler(std::unique_ptr<IProtocolHandler> handler);

    // Begin monitoring for device attach/detach events.
    // On Linux uses a hotplug callback thread; on RP2040 called from
    // the main loop tick.
    void start();
    void stop();

    // Synchronously submit a job. Performs protocol auto-detection if
    // detectedProtocol == ProtocolType::UNKNOWN. Thread-safe on Linux.
    bool submitJob(PrintJob job,
                   ProtocolType hint = ProtocolType::UNKNOWN);

    // Returns the currently active device, or nullopt if none attached.
    std::optional<UsbDeviceDescriptor> activeDevice() const;

    // Callback invoked on attach/detach. Set before calling start().
    std::function<void(const UsbDeviceDescriptor&, bool /*attached*/)>
        onDeviceChange;

private:
    // ...
};
```

---

## 5. Phase 1 — Linux TSPL Prototype

**Goal:** The simplest possible working system: a Linux command-line tool that accepts a pre-formed TSPL byte stream (from a file or stdin) and sends it to the first detected label printer over USB. Proves out the transport and protocol core before any scaffolding complexity is added.

### 5.1 Scope

- Linux only; single device; no hot-plug.
- `LinuxUsbTransport` backed by **libusb-1.0**.
- `TsplHandler` supporting the minimum TSPL/TSPL-2 command set needed to send a complete label job.
- CLI: `tspl_print [--vid VID] [--pid PID] <job_file_or_stdin>`
- Basic logging to stderr (Logger utility, no external dependency).

### 5.2 `LinuxUsbTransport` (Phase 1)

- Uses `libusb_open_device_with_vid_pid()` for simplicity in Phase 1.
- Detects printer-class interface (bInterfaceClass = 0x07) and claims it.
- Finds the first BULK OUT and BULK IN endpoints.
- `write()` wraps `libusb_bulk_transfer()`.
- `read()` wraps `libusb_bulk_transfer()` on the IN endpoint; returns 0 bytes (not an error) if the endpoint is write-only (some label printers).
- Kernel driver auto-detach via `libusb_set_auto_detach_kernel_driver()`.

**Error handling:** All libusb errors map to a `TransportError` enum (Timeout, Disconnected, AccessDenied, Unknown) logged before returning -1.

### 5.3 `TsplHandler` (Phase 1)

The TSPL handler in Phase 1 is intentionally thin — its job is to forward an already-formed TSPL byte stream, not to generate TSPL from higher-level primitives.

**`probe()`** returns `true` if data begins with any of:
- `SIZE` (TSPL SIZE command)
- `^XA` (ZPL preamble, treated as compatible)
- `\x1b\x40` (TSPL ESC-@ reset)

**`sendJob()`** splits the raw data into chunks of `TSPL_CHUNK_SIZE` (default 4096 bytes) and calls `transport.write()` for each. After the final chunk a configurable inter-job delay (default 100 ms) is inserted before returning.

**`queryStatus()`** sends the TSPL `~HS` (host status) query, reads up to 256 bytes, and returns the raw ASCII response string. Returns `false` (non-fatal) if no IN endpoint is available.

### 5.4 Build (Phase 1)

```cmake
# cmake/platform_linux.cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
target_link_libraries(printer_driver_linux PRIVATE ${LIBUSB_LIBRARIES})
target_include_directories(printer_driver_linux PRIVATE ${LIBUSB_INCLUDE_DIRS})
target_compile_definitions(printer_driver_linux PRIVATE PLATFORM_LINUX)
```

### 5.5 Phase 1 Acceptance Criteria

- Sends a TSPL file to a Zebra-compatible label printer and a label is produced.
- Graceful error message (not a crash) when no printer is found.
- Graceful error message when file is not readable.
- Unit tests for `TsplHandler::probe()` with positive and negative fixtures.

---

## 6. Phase 2 — Multi-Device Scaffolding + RP2040 TSPL

**Goal:** Introduce the full `PrinterManager` hot-plug architecture and port TSPL support to the RP2040 via TinyUSB. Linux TSPL capability is preserved throughout.

### 6.1 Scope

- `PrinterManager` with hot-plug, device registry, and protocol dispatch.
- `LinuxUsbTransport` upgraded with libusb hotplug callback thread.
- `Rp2040UsbTransport` backed by **TinyUSB** (host mode).
- Multi-device registry (support up to `MAX_DEVICES`, default 4 on RP2040, unlimited on Linux).
- Protocol auto-detection via `probe()` on first chunk of each job.
- `TsplHandler` unchanged from Phase 1 — no new TSPL features yet.

### 6.2 Hot-Plug Design

**Linux:** `libusb_hotplug_register_callback()` fires `PrinterManager::onAttach()` / `onDetach()` from a dedicated event thread. A `std::mutex` protects the device registry. The callback checks `bDeviceClass` or interface class for printer-class devices (class 0x07) before registering.

**RP2040:** TinyUSB's `tuh_hid_mount_cb()` / `tuh_hid_umount_cb()` are re-targeted to printer class endpoints. Since the RP2040 is single-core-tasked (or uses the second core for USB), there is no threading — hot-plug is polled via `tuh_task()` called from the main loop. The device registry is a fixed-size array of `DeviceRecord`.

### 6.3 `DeviceRecord`

```cpp
struct DeviceRecord {
    UsbDeviceDescriptor descriptor;
    ProtocolType        detectedProtocol = ProtocolType::UNKNOWN;
    bool                active           = false;
    uint32_t            attachTimestamp  = 0; // ms since boot
};
```

### 6.4 Protocol Auto-Detection Flow

When `PrinterManager::submitJob()` is called with `hint == UNKNOWN`:

1. If the target `DeviceRecord` already has a `detectedProtocol` from a previous job, use it directly (cached).
2. Otherwise peek the first 64 bytes of `job.rawData` and call `probe()` on each registered handler in registration order.
3. First handler to return `true` wins; result is cached in `DeviceRecord`.
4. If no handler matches, log a warning and return `false`.

### 6.5 `Rp2040UsbTransport`

- Wraps TinyUSB host bulk pipe APIs (`tuh_bulk_write()`, `tuh_bulk_read()`).
- `open()` stores the TinyUSB device address and discovers printer-class interface/endpoints using `tuh_descriptor_get_interface()`.
- `write()` uses a semaphore-based completion flag (TinyUSB transfer callback sets it; caller polls with timeout).
- Memory: all buffers allocated statically or from a fixed pool — no heap allocation on RP2040 transport layer.
- `MAX_PACKET_SIZE` defined at compile time (default 512 for USB HS, 64 for FS).

### 6.6 `RingBuffer<T, N>` Utility

A lock-free (single producer / single consumer) ring buffer used for the RP2040 job queue and the Linux log sink. Template parameters `T` (element type) and `N` (capacity, must be power of 2) are compile-time constants. No dynamic allocation.

### 6.7 Build System (Phase 2 additions)

```cmake
# Top-level CMakeLists.txt
option(TARGET_RP2040 "Build for RP2040 via pico-sdk" OFF)

if(TARGET_RP2040)
    include(cmake/platform_rp2040.cmake)
else()
    include(cmake/platform_linux.cmake)
endif()
```

The RP2040 build requires `PICO_SDK_PATH` to be set in the environment. The pico-sdk `tinyusb` component is included via the standard pico-sdk CMake machinery.

### 6.8 Phase 2 Acceptance Criteria

- Linux: plug in a label printer mid-run; a queued job is dispatched without restart.
- Linux: unplug and re-plug; driver re-attaches and next job succeeds.
- RP2040: TSPL label job completes successfully over USB host.
- RP2040: plugging a second printer (if hardware supports it) registers a second `DeviceRecord` without corrupting the first.
- Protocol auto-detection unit test: TSPL probe fires correctly; a ZPL stream is also accepted.
- Mock transport unit tests for `PrinterManager` attach/detach state machine.

---

## 7. Phase 3 — PCL and ESC/P Protocol Support

**Goal:** Add `PclHandler` and `EscpHandler` with the protocol-specific logic needed to send jobs to common laser/inkjet (PCL) and receipt/dot-matrix (ESC/P) printers. Both Linux and RP2040 targets are supported.

### 7.1 PCL Subset (`PclHandler`)

Target: PCL 5e subset sufficient for single-page text and simple raster graphics (sufficient for typical report/receipt printing scenarios).

**`probe()`** returns `true` if data begins with `\x1b%-12345X` (PJL universal exit) or `\x1bE` (PCL reset) within the first 16 bytes.

**Supported PCL command categories:**

- Job control: `\x1bE` (reset), `\x1b%-12345X@PJL` wrapper (optional passthrough).
- Page setup: paper size (`&l`), orientation (`&l`), margins (`&a`).
- Raster graphics: `*r` (raster begin/end), `*b` (transfer raster data) — used for pre-rasterized image payloads.
- Text: font selection (`(s`), line termination (CR/LF passthrough).

**`sendJob()`** sends the raw PCL stream chunked identically to `TsplHandler`. No PCL parsing is performed — the handler is a validated forwarder. Validation = `probe()` check on entry.

**`queryStatus()`** sends a PJL `@PJL INFO STATUS` query if the device supports a bidirectional channel; otherwise returns an empty string and `false`.

### 7.2 ESC/P Subset (`EscpHandler`)

Target: ESC/P 2 subset (Epson receipt/label/dot-matrix). Also covers ESC/POS for receipt printers (which share the ESC command prefix).

**`probe()`** returns `true` if data begins with `\x1b@` (ESC/P initialize) or `\x1b\x50` (ESC P — select printer) within the first 8 bytes, OR if data begins with `\x1d` (GS — ESC/POS GS class command, common for receipt printers).

**Supported ESC/P command categories:**

- Initialization: `\x1b@`.
- Line feed / carriage return: `\x0a`, `\x0d`.
- Character pitch/font: `\x1bP`, `\x1bM`.
- Bit image printing: `\x1bK`, `\x1bL` (standard density), `\x1b*` (ESC/P2 graphics).
- Paper cut (ESC/POS): `\x1dV` — forwarded as-is.

**`sendJob()`** sends chunked, with optional inter-line delay (`ESCP_LINE_DELAY_MS`, default 0, configurable for slow dot-matrix printers).

**`queryStatus()`** sends `\x1bv` (real-time status) and reads up to 4 bytes per the ESC/POS real-time status response format. Returns `false` gracefully if no response arrives within timeout.

### 7.3 Protocol Handler Registration Order

Handlers must be registered in specificity order (most specific first) since the first `probe()` match wins:

```cpp
manager.registerHandler(std::make_unique<TsplHandler>());
manager.registerHandler(std::make_unique<EscpHandler>());
manager.registerHandler(std::make_unique<PclHandler>());
```

This ordering ensures TSPL and ESC/P (which have distinct magic bytes) are matched before PCL's more permissive probe.

### 7.4 IEEE 1284 Device ID Assisted Detection

When a `UsbDeviceDescriptor` carries a non-empty `ieee1284Id` string, `PrinterManager` performs a preliminary protocol hint extraction before calling `probe()`:

- String contains `"COMMAND SET:TSPL"` or `"CMD:ZPL"` → hint `TSPL`.
- String contains `"COMMAND SET:PCL"` → hint `PCL`.
- String contains `"COMMAND SET:ESCPL"` or `"CMD:ESC/P"` → hint `ESCP`.

The hint bypasses the `probe()` loop and goes directly to the matching handler. This is particularly useful on RP2040 where avoiding unnecessary probe iterations saves cycles.

### 7.5 Phase 3 Acceptance Criteria

- PCL: single-page text job prints successfully on a PCL-capable inkjet or laser printer (Linux).
- PCL: `PclHandler::probe()` rejects a TSPL stream and an ESC/P stream in unit tests.
- ESC/P: receipt job prints and paper cut is executed on an ESC/POS receipt printer (Linux).
- ESC/P: `EscpHandler::probe()` rejects PCL and TSPL streams in unit tests.
- RP2040: all three protocol handlers compile and link within RP2040 flash/RAM budget (verify with `size` output; target <200 KB flash for protocol+transport layer combined).
- IEEE 1284 assisted detection unit test: all three hint paths exercised.

---

## 8. Logging

A compile-time-selectable `Logger` class provides leveled output (`DEBUG`, `INFO`, `WARN`, `ERROR`).

- **Linux:** logs to `stderr` with timestamps via `std::chrono`.
- **RP2040:** logs via `printf()` to UART (pico-sdk stdio). The `DEBUG` level is compiled out when `NDEBUG` is defined to save flash.
- Log format: `[LEVEL][timestamp_ms][component] message`
- No heap allocation in the RP2040 log path; format strings use `snprintf()` into a stack buffer.

---

## 9. Error Handling Strategy

| Situation | Linux Behavior | RP2040 Behavior |
|---|---|---|
| Device not found on open | Log ERROR, return false | Log ERROR, retry after 500ms |
| Write timeout | Log WARN, return -1 | Log WARN, reset endpoint, return -1 |
| Device disconnected mid-job | Log WARN, close, fire onDetach | Log WARN, mark record inactive |
| No protocol match | Log ERROR, return false | Log ERROR, return false |
| Unknown USB error | Log ERROR with libusb/TinyUSB code | Log ERROR with TinyUSB code |

No exceptions are used. Error propagation is via return values and the `Logger`. This ensures the code compiles cleanly with `-fno-exceptions` required for RP2040 builds.

---

## 10. Testing Strategy

All tests use **Catch2** (header-only, included as a submodule) and compile against the Linux platform only. RP2040-specific behavior is covered by the `MockUsbTransport`.

`MockUsbTransport` implements `IUsbTransport` with configurable failure injection:
- Simulate write timeout.
- Simulate mid-job disconnect.
- Capture written bytes for assertion.
- Return configurable status byte sequences from `read()`.

**Test files and their focus:**

- `test_tspl_handler.cpp` — probe(), sendJob() chunking, queryStatus() parsing.
- `test_pcl_handler.cpp` — probe() positive/negative, chunked send, PJL status parse.
- `test_escp_handler.cpp` — probe() for ESC/P and ESC/POS variants, sendJob(), real-time status.
- `test_printer_manager.cpp` — attach/detach state machine, protocol auto-detection, job dispatch, IEEE 1284 hints.

CI target: `ctest --output-on-failure` run on every commit via GitHub Actions.

---

## 11. Dependencies Summary

| Dependency | Version | Used By | Notes |
|---|---|---|---|
| libusb-1.0 | ≥ 1.0.24 | Linux transport | System package |
| TinyUSB | bundled in pico-sdk | RP2040 transport | Host mode |
| pico-sdk | ≥ 1.5.1 | RP2040 platform | Set PICO_SDK_PATH |
| Catch2 | v3.x (header) | Tests only | Git submodule |
| CMake | ≥ 3.20 | Build | — |
| GCC / Clang | C++17 | Linux | -std=c++17 |
| arm-none-eabi-gcc | ≥ 12 | RP2040 | pico-sdk toolchain |

No other third-party dependencies. The protocol handlers and manager are dependency-free beyond the C++17 standard library.

---

## 13. Phase 4 — Text Generator Layer

**Goal:** Add a language generator layer above the protocol handlers so that application code (and Phase 5's SIO emulator) can produce printed output from plain text lines without constructing raw command bytes manually. Phase 4 covers text-only output — line printer style. Raster graphics are deferred to Phase 6.

### 13.1 Scope

- `ITextGenerator` abstract interface.
- `TsplTextGenerator`, `PclTextGenerator`, `EscpTextGenerator` concrete implementations.
- Each generator produces a `PrintJob` (raw byte stream) consumable by the existing `PrinterManager` / protocol handler pipeline with no changes to Phase 1–3 code.
- Text configuration: font size, character pitch (where the protocol supports it), page/label dimensions.
- No graphics, no barcodes, no images — those are Phase 6.

### 13.2 Repository Additions

```
include/
  generator/
    ITextGenerator.h
    TextConfig.h
    TsplTextGenerator.h
    PclTextGenerator.h
    EscpTextGenerator.h
src/
  generator/
    TsplTextGenerator.cpp
    PclTextGenerator.cpp
    EscpTextGenerator.cpp
tests/
  test_tspl_text_generator.cpp
  test_pcl_text_generator.cpp
  test_escp_text_generator.cpp
```

### 13.3 `TextConfig`

```cpp
// include/generator/TextConfig.h
struct TextConfig {
    // Page / label dimensions (used by TSPL; ignored by ESC/P line mode)
    uint16_t labelWidthDots   = 400;   // TSPL: label width in dots
    uint16_t labelHeightDots  = 240;   // TSPL: label height in dots
    uint8_t  gapDots          = 24;    // TSPL: inter-label gap

    // Text placement (TSPL)
    uint16_t marginLeftDots   = 10;
    uint16_t marginTopDots    = 10;
    uint8_t  tsplFontId       = 3;     // TSPL built-in font (1–8)
    uint8_t  tsplFontXMul     = 1;     // X multiplier
    uint8_t  tsplFontYMul     = 1;     // Y multiplier

    // PCL text settings
    uint8_t  pclLinesPerPage  = 60;
    uint8_t  pclCpi           = 10;    // Characters per inch (10 or 12)
    uint8_t  pclLpi           = 6;     // Lines per inch (6 or 8)

    // ESC/P settings
    bool     escpCondensed    = false; // true → 17 CPI condensed mode
    uint8_t  escpLpi          = 6;

    // Common
    std::string jobId;
};
```

### 13.4 `ITextGenerator`

```cpp
// include/generator/ITextGenerator.h
class ITextGenerator {
public:
    virtual ~ITextGenerator() = default;

    // Configure the generator. Must be called before any writeLine().
    virtual void configure(const TextConfig& config) = 0;

    // Append one line of plain ASCII text. Implementations handle
    // line termination internally per protocol.
    // For TSPL: line is buffered until flush().
    // For PCL and ESC/P: line may be written to the internal buffer
    // immediately; flush() finalises the job.
    virtual void writeLine(std::string_view line) = 0;

    // Write a blank line (equivalent to writeLine("")).
    virtual void writeBlank() = 0;

    // Finalise the current page/label and return a PrintJob ready
    // for PrinterManager::submitJob(). Resets internal state so the
    // generator is ready for the next page/label.
    virtual PrintJob flush() = 0;

    // Discard buffered content without producing a job.
    virtual void reset() = 0;

    // Protocol type this generator targets.
    virtual ProtocolType protocol() const = 0;
};
```

### 13.5 `TsplTextGenerator`

TSPL is page-oriented: the entire label must be described before the `PRINT` command is issued. Lines are accumulated in an internal buffer and the complete label command stream is assembled in `flush()`.

**`writeLine()` behaviour:**

Each call appends a `TEXT` command at the next Y position:

```
TEXT {marginLeft},{currentY},"TSS24.BF2",0,{xMul},{yMul},"{line}\r\n"
```

`currentY` advances by `lineHeightDots` (derived from font size × Y multiplier, default 24 dots) on each call.

**`flush()` output sequence:**

```
SIZE {labelWidthMm} mm,{labelHeightMm} mm\r\n
GAP {gapMm} mm,0 mm\r\n
DIRECTION 0\r\n
CLS\r\n
TEXT ...  (one per buffered line)
...
PRINT 1,1\r\n
```

Dot-to-mm conversion uses 8 dots/mm (203 dpi, standard label printer). A `constexpr` conversion helper `dotsToMm(uint16_t dots)` is provided in `TsplTextGenerator.cpp`.

If `writeLine()` is called enough times that `currentY` would exceed `labelHeightDots`, the current label is auto-flushed and a new label starts — effectively automatic page breaking.

**`reset()`** clears the line buffer and resets `currentY` to `marginTopDots`.

### 13.6 `PclTextGenerator`

PCL in line-printer mode is straightforward: each line is a text string followed by CR+LF. The job is wrapped in a minimal PCL header and footer.

**`flush()` output sequence:**

```
\x1bE                        PCL reset
\x1b&l0O                     Portrait orientation
\x1b&l{lpi}D                 Lines per inch
\x1b(10U                     PC-8 symbol set
\x1b(s0p{cpi}h12v0s0b3T      Courier font, CPI, 12pt, upright, medium, Courier
{line 1}\r\n
{line 2}\r\n
...
\x0c                         Form feed (eject page)
\x1bE                        PCL reset
```

**`writeLine()`** appends the line string and `\r\n` to an internal `std::vector<uint8_t>` body buffer. Header and footer bytes are prepended/appended in `flush()`.

**Page breaking:** when the line count reaches `pclLinesPerPage`, a form feed `\x0c` is inserted and the line count resets. This happens within `flush()` — the entire accumulated content is scanned to insert form feeds at the right positions.

### 13.7 `EscpTextGenerator`

ESC/P in line-printer mode writes each line immediately to the internal buffer. This maps naturally to the Atari's line-at-a-time SIO writes.

**`configure()` initialisation sequence (emitted at start of first `writeLine()` or `flush()`):**

```
\x1b@                        ESC/P initialize
\x1bx1                       NLQ mode (if not condensed)
\x1bP  or  \x1b\x0f          10 CPI normal, or 17 CPI condensed
\x1b3{n}                     Set line spacing (n = 216/lpi)
```

**`writeLine()`** appends `line + \r\n` to the buffer. If `escpCondensed` is true, a condensed-on byte `\x0f` is prepended in `configure()`.

**`flush()`** appends `\x0c` (form feed) to eject the page and returns the complete job. For receipt-printer use (no form feed desired), a `bool suppressFormFeed` field in `TextConfig` suppresses the `\x0c`.

**`reset()`** clears the buffer but does not re-emit the init sequence — `configure()` must be called again or the init flag re-armed if a fresh init is needed.

### 13.8 Generator Factory

A convenience factory function is provided so callers don't need to know the concrete type:

```cpp
// include/generator/ITextGenerator.h
std::unique_ptr<ITextGenerator> makeTextGenerator(ProtocolType proto);
```

This is used by the Phase 5 SIO emulator to instantiate the correct generator based on the configured target printer personality.

### 13.9 Phase 4 Acceptance Criteria

- `TsplTextGenerator`: a 5-line text label flushes as a valid TSPL job; prints correctly on a label printer from Linux CLI.
- `PclTextGenerator`: a 70-line job auto-inserts a form feed at line 60; output is valid PCL verified by a PCL viewer or printer.
- `EscpTextGenerator`: a 10-line job followed by `flush()` produces correct ESC/P bytes verified against a byte-level test fixture.
- All three generators: `reset()` followed by new `writeLine()` calls produces a clean job with no residue from the previous one.
- All three generators: empty `flush()` (no `writeLine()` calls) returns an empty `PrintJob` (zero-length `rawData`) without crashing.
- Unit tests cover: normal flow, auto page-break (TSPL and PCL), condensed ESC/P, `suppressFormFeed` path.

---

## 14. Phase 5 — Atari SIO Printer Emulator on RP2040

**Goal:** The RP2040 presents itself to an Atari 8-bit computer as a printer on the SIO bus, receives printer data records, assembles lines, translates ATASCII to ASCII, feeds them through the Phase 4 text generators, and submits completed pages/labels to the USB printer via the Phase 1–3 driver stack. This is the primary end-to-end use case for the project.

### 14.1 Background and Constraints

The Atari SIO (Serial I/O) bus runs at **19,200 baud**, 8N1. The RP2040 connects via a UART peripheral plus a GPIO for the SIO **command line** (active-low signal that gates command frames). This is the same electrical approach used by SIO2Arduino and FujiNet; those source bases are available for reference during implementation.

The Atari OS ROM issues printer writes as fixed-size **40-byte records** regardless of actual line length. The end of meaningful content within a record is marked by `$9B` (ATASCII EOL). Bytes after `$9B` are right-padding spaces and are discarded. All three emulated printer personalities (825, 1025, 1027) use this same record structure because the write routine is in the Atari OS ROM, not the printer ROM.

The 1025 is an 80-column printer. The Atari achieves 80 columns by issuing **two consecutive 40-byte SIO writes** per printed line. The emulator must recognise this and join the two records before passing the assembled line to the generator.

### 14.2 Emulated Printer Personalities

| Personality | Columns | Generator Used | Notes |
|---|---|---|---|
| 825 | 40 | `EscpTextGenerator` | 40-col dot matrix; ESC/P is closest match |
| 1025 | 80 | `PclTextGenerator` | 80-col; two 40-byte records per line |
| 1027 | 40 | `EscpTextGenerator` | Epson-compatible; ESC/P direct mapping |
| TSPL Label | configurable | `TsplTextGenerator` | Non-Atari mode; label printer target |

Personality is selected at **compile time** via a `#define ATARI_PRINTER_PERSONALITY` (825, 1025, 1027, or TSPL). Runtime switching is not required in Phase 5 but should not be architecturally precluded.

### 14.3 Repository Additions

```
include/
  sio/
    SioProtocol.h          // SIO framing constants and helpers
    SioCommandFrame.h      // Command frame struct
    SioPrinterEmulator.h   // Top-level SIO device emulator
    LineAssembler.h        // Record → line assembly + ATASCII conversion
    AtasciiConverter.h     // ATASCII → ASCII translation table
src/
  sio/
    SioProtocol.cpp
    SioPrinterEmulator.cpp
    LineAssembler.cpp
    AtasciiConverter.cpp
platform/
  rp2040/
    SioUart.h              // UART + GPIO wiring for SIO
    SioUart.cpp
tests/
  test_line_assembler.cpp
  test_atascii_converter.cpp
  test_sio_printer_emulator.cpp  // Uses mock SIO byte injector
```

### 14.4 SIO Protocol Constants

```cpp
// include/sio/SioProtocol.h
namespace Sio {
    constexpr uint32_t BAUD_RATE        = 19200;
    constexpr uint8_t  CMD_WRITE        = 0x57;  // 'W'
    constexpr uint8_t  ACK             = 0x41;  // 'A'
    constexpr uint8_t  NAK             = 0x4E;  // 'N'
    constexpr uint8_t  COMPLETE        = 0x43;  // 'C'
    constexpr uint8_t  ERROR_RESP      = 0x45;  // 'E'
    constexpr uint8_t  DEVICE_P1       = 0x40;
    constexpr uint8_t  DEVICE_P2       = 0x41;
    constexpr uint8_t  DEVICE_P3       = 0x42;
    constexpr uint8_t  DEVICE_P4       = 0x43;
    constexpr uint8_t  RECORD_SIZE     = 40;
    constexpr uint8_t  ATASCII_EOL     = 0x9B;
    constexpr uint32_t CMD_LINE_GPIO   = 26;    // default; override in SioUart config
    // Timing constants (microseconds) per Atari SIO spec
    constexpr uint32_t T_COMPLETE_US   = 250;   // delay before COMPLETE
    constexpr uint32_t T_ACK_US        = 850;   // delay before ACK after command
}
```

### 14.5 `SioCommandFrame`

```cpp
struct SioCommandFrame {
    uint8_t deviceId;
    uint8_t command;
    uint8_t aux1;
    uint8_t aux2;
    uint8_t checksum;

    bool isValid() const;        // verifies checksum
    bool isPrinterDevice() const; // deviceId in $40–$43
};
```

Checksum is the standard SIO sum-of-bytes mod 256 with carry added back.

### 14.6 `SioUart` (RP2040 Platform Layer)

`SioUart` owns the UART peripheral and command-line GPIO. It is the only file in the SIO subsystem that touches RP2040 hardware registers directly.

```cpp
// platform/rp2040/SioUart.h
class SioUart {
public:
    struct Config {
        uart_inst_t* uart     = uart1;    // UART1 by default (pins 4/5)
        uint         txPin    = 4;
        uint         rxPin    = 5;
        uint         cmdPin   = 26;       // SIO command line GPIO
        uint32_t     baudRate = Sio::BAUD_RATE;
    };

    explicit SioUart(const Config& cfg);

    void     init();
    bool     cmdLineAsserted() const;    // true when command line is low
    bool     readByte(uint8_t& out, uint32_t timeoutUs);
    void     writeByte(uint8_t b);
    void     writeBytes(const uint8_t* data, size_t len);
    void     flushTx();
};
```

The command line GPIO is configured as input with pull-up. An interrupt-on-falling-edge is registered to set an internal flag, which `SioPrinterEmulator` polls via `cmdLineAsserted()`.

### 14.7 `SioPrinterEmulator` State Machine

The emulator runs as a state machine driven by bytes from `SioUart`. It lives in the RP2040 main loop (or second core).

```
IDLE
  │  CMD line asserted
  ▼
RECV_CMD_FRAME (read 5 bytes)
  │  checksum OK, isPrinterDevice, command == CMD_WRITE
  ▼
SEND_ACK  (wait T_ACK_US, send ACK)
  │
  ▼
RECV_RECORD (read RECORD_SIZE bytes + 1 checksum byte)
  │  checksum OK
  ▼
SEND_ACK  (send ACK immediately)
  │
  ▼
PROCESS_RECORD → LineAssembler::ingest()
  │
  ▼
SEND_COMPLETE (wait T_COMPLETE_US, send COMPLETE)
  │
  └──────────────────────────────────────────────► IDLE
```

On any checksum failure or timeout, the emulator sends NAK and returns to IDLE.

**Main loop integration:**

```cpp
// platform/rp2040/main_rp2040.cpp (Phase 5 additions)
while (true) {
    tuh_task();                    // TinyUSB host tick
    sioEmulator.tick();            // SIO state machine tick
    printerManager.tick();         // Hot-plug polling tick (Phase 2)
}
```

`SioPrinterEmulator::tick()` is non-blocking — it reads available UART bytes and advances the state machine without busy-waiting.

### 14.8 `LineAssembler`

`LineAssembler` absorbs one or two 40-byte records and emits complete lines to the text generator.

```cpp
class LineAssembler {
public:
    enum class Mode { COL_40, COL_80 };

    explicit LineAssembler(Mode mode, ITextGenerator& generator);

    // Feed one 40-byte record. In COL_80 mode, the first record is
    // buffered; the second triggers line assembly and generator write.
    // In COL_40 mode, each record triggers a line assembly immediately.
    void ingest(const uint8_t* record, uint8_t len);

    // Force-flush any buffered half-line (COL_80 mode: if the Atari
    // sends only one record before a page break, emit what we have).
    void flush();

    // Reset without emitting.
    void reset();

private:
    // Scans record for $9B; strips everything from $9B onward;
    // strips trailing spaces; converts via AtasciiConverter.
    std::string processRecord(const uint8_t* record, uint8_t len);

    Mode             mode_;
    ITextGenerator&  generator_;
    std::string      halfLine_;   // COL_80: holds first 40-char segment
    bool             halfPending_ = false;
};
```

**`processRecord()` algorithm:**

1. Find first `$9B` in the record. If found, `effectiveLen = index of $9B`. If not found, `effectiveLen = len`.
2. Strip trailing spaces from `[0, effectiveLen)`.
3. Pass each byte through `AtasciiConverter::toAscii()`.
4. Return the resulting `std::string`.

**COL_80 join logic:** In `COL_80` mode, the first `ingest()` call stores the processed string in `halfLine_`. The second call appends its processed string to `halfLine_` and calls `generator_.writeLine(halfLine_)`, then clears `halfLine_`.

If the first record contained a `$9B`, the line ended within the first 40 columns. `halfLine_` is emitted immediately without waiting for the second record, and the second record (which will be padding) is discarded.

### 14.9 `AtasciiConverter`

A lookup table mapping ATASCII byte values `$00`–`$FF` to ASCII equivalents.

```cpp
// include/sio/AtasciiConverter.h
class AtasciiConverter {
public:
    // Convert a single ATASCII byte to its closest ASCII equivalent.
    // Returns '?' for characters with no reasonable ASCII mapping.
    static char toAscii(uint8_t atascii);

    // Convert an entire ATASCII string in-place.
    static void convertBuffer(uint8_t* buf, size_t len);
};
```

**Phase 5 translation table** covers the minimum required for text printing:

| ATASCII Range | Mapping |
|---|---|
| `$20`–`$5A` | Direct (same as ASCII printable + uppercase) |
| `$61`–`$7A` | Lowercase letters (ATASCII matches ASCII) |
| `$00`–`$1F` | Control codes: `$09`→Tab, `$0A`→LF, `$0D`→CR; rest → space |
| `$9B` | Line terminator — handled by `LineAssembler`, never reaches converter |
| `$80`–`$9A` | Inverse video versions of `$00`–`$1A` → strip inverse, return base char |
| `$A0`–`$FE` | International/graphics characters → '?' placeholder in Phase 5; full mapping in Phase 6 |

The full ATASCII→Unicode→ASCII mapping (covering international characters, ATASCII graphics, and inverse-video equivalents) is deferred to Phase 6 alongside graphics support.

### 14.10 TSPL Personality Specifics

When `ATARI_PRINTER_PERSONALITY == TSPL`, the flow differs because TSPL labels must be fully described before printing:

- `LineAssembler` feeds lines to `TsplTextGenerator` exactly as with other personalities.
- The `TsplTextGenerator` auto-flushes (triggers a `PrinterManager::submitJob()` call) when:
  - The label height is exhausted (existing Phase 4 auto-page-break logic), **or**
  - A configurable **idle timeout** (`TSPL_FLUSH_TIMEOUT_MS`, default 2000 ms) elapses with no new `writeLine()` call. This handles the case where the Atari finishes printing a label but doesn't send an explicit page-break command.

The idle timeout is managed by `SioPrinterEmulator`, which tracks the timestamp of the last `ingest()` call and calls `generator_.flush()` + `printerManager_.submitJob()` when the timeout fires.

```cpp
// In SioPrinterEmulator::tick():
if (tsplIdleTimeoutEnabled_ &&
    lastIngestMs_ > 0 &&
    (millis() - lastIngestMs_) > TSPL_FLUSH_TIMEOUT_MS) {
    auto job = generator_->flush();
    if (!job.rawData.empty())
        printerManager_.submitJob(std::move(job));
    lastIngestMs_ = 0;
}
```

### 14.11 End-to-End Data Flow (Phase 5)

```
Atari 8-bit Computer
        │  SIO bus (19200 baud, 8N1, CMD line)
        ▼
   SioUart (UART1 + GPIO26 on RP2040)
        │  raw bytes
        ▼
   SioPrinterEmulator (state machine)
        │  40-byte records
        ▼
   LineAssembler (record → line, ATASCII strip)
        │  std::string lines
        ▼
   AtasciiConverter (ATASCII → ASCII)
        │  ASCII lines
        ▼
   ITextGenerator (TSPL / PCL / ESC/P)
        │  PrintJob (complete command stream)
        ▼
   PrinterManager (protocol dispatch, hot-plug)
        │  IUsbTransport
        ▼
   Rp2040UsbTransport (TinyUSB host)
        │  USB bulk transfer
        ▼
   Physical USB Printer
```

### 14.12 Phase 5 Acceptance Criteria

- Atari → 825 personality: a BASIC `LPRINT "HELLO WORLD"` produces a correctly printed line on an ESC/P-capable USB printer connected to the RP2040.
- Atari → 1025 personality: an 80-column line (requires two SIO writes) is correctly joined and printed as a single line.
- Atari → 1027 personality: ESC/P output is functionally identical to 825 (confirmed by byte-level test fixture, not requiring physical hardware).
- Atari → TSPL personality: three consecutive `LPRINT` lines followed by 2 seconds of idle triggers a label print with all three lines on the label.
- `LineAssembler`: `$9B` mid-record correctly terminates the line and discards padding — unit tested with byte fixtures.
- `LineAssembler`: a record with no `$9B` (all 40 bytes are content) is handled without out-of-bounds access.
- `AtasciiConverter`: inverse-video bytes (`$80`–`$9A`) return the correct base character — unit tested.
- `SioPrinterEmulator`: a simulated bad-checksum command frame results in NAK and return to IDLE — unit tested with mock SIO byte injector.
- `SioPrinterEmulator`: a simulated mid-record disconnect (timeout waiting for record bytes) sends NAK and returns to IDLE.
- RP2040 build: total firmware fits within RP2040 flash (2 MB); Phase 5 additions add no more than 32 KB to the Phase 2 baseline.

---

## 15. Implementation Sequence for Claude Code

Claude Code should implement this project in strict phase order. Each phase ends with all acceptance criteria passing before the next phase begins.

**Phase 1 sequence:**
1. Scaffold CMake structure and `platform_linux.cmake`.
2. Implement `Logger`, `UsbDeviceDescriptor`, `PrintJob`.
3. Implement `IUsbTransport` and `IProtocolHandler` interfaces.
4. Implement `LinuxUsbTransport` (single-device, no hotplug).
5. Implement `TsplHandler`.
6. Implement `main_linux.cpp` CLI.
7. Write Phase 1 unit tests; confirm all pass.

**Phase 2 sequence:**
1. Implement `RingBuffer` utility.
2. Upgrade `PrinterManager` with device registry and hotplug.
3. Upgrade `LinuxUsbTransport` with libusb hotplug callbacks.
4. Implement `Rp2040UsbTransport` and `platform_rp2040.cmake`.
5. Implement `main_rp2040.cpp` main loop.
6. Write Phase 2 unit tests; confirm all pass.

**Phase 3 sequence:**
1. Implement `PclHandler`.
2. Implement `EscpHandler`.
3. Add IEEE 1284 detection logic to `PrinterManager`.
4. Update handler registration in both `main_linux.cpp` and `main_rp2040.cpp`.
5. Write Phase 3 unit tests; confirm all pass.
6. Verify RP2040 flash/RAM budget with `arm-none-eabi-size`.

**Phase 4 sequence:**
1. Define `TextConfig` and `ITextGenerator` interface.
2. Implement `TsplTextGenerator` including dot/mm conversion and auto page-break.
3. Implement `PclTextGenerator` including form-feed insertion.
4. Implement `EscpTextGenerator` including condensed mode and `suppressFormFeed`.
5. Implement `makeTextGenerator()` factory function.
6. Integrate generators into `main_linux.cpp` for smoke testing from the command line.
7. Write Phase 4 unit tests; confirm all pass.

**Phase 5 sequence:**
1. Implement `AtasciiConverter` lookup table and unit tests.
2. Implement `LineAssembler` (COL_40 mode first, then COL_80).
3. Implement `SioProtocol` constants and `SioCommandFrame` with checksum validation.
4. Implement `SioUart` (RP2040 platform layer: UART init, GPIO command line, read/write).
5. Implement `SioPrinterEmulator` state machine.
6. Add TSPL idle-timeout flush logic to `SioPrinterEmulator`.
7. Integrate `SioPrinterEmulator::tick()` into `main_rp2040.cpp` main loop.
8. Write Phase 5 unit tests using mock SIO byte injector; confirm all pass.
9. End-to-end hardware test: Atari → RP2040 → USB printer for each personality.
10. Final RP2040 flash/RAM budget check with `arm-none-eabi-size`.

---

*End of specification.*
