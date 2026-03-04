# Phase 2 Hardware Testing Guide

**Binary:** `build/tspl_print`
**Focus:** Multi-device registration, protocol auto-detection, DeviceRecord lifecycle
**Requirement:** At minimum one USB printer (TSPL label printer recommended). A second USB printer of any protocol is optional for multi-device tests.

> **Note on hotplug callbacks:** `main_linux.cpp` does not call `manager.start()`, so libusb hotplug event callbacks are not active in the CLI binary. Plug/unplug detection is exercised by running `--list` between plug and unplug events (each invocation calls `enumeratePrinters()` fresh). This is the correct test path for Phase 2 hardware validation.

---

## 1. Prerequisites

### 1.1 Hardware

- A TSPL/ZPL-compatible label printer (e.g. Zebra ZD421, TSC TTP-244, Godex EZ-series, AiYin) connected via USB
- Optional: a second USB printer of a different protocol (any PCL or ESC/P device) for multi-device tests
- USB cables for each printer

### 1.2 Software

```bash
# Ensure libusb is installed
dpkg -l libusb-1.0-0 | grep -E "^ii"

# Build the project (from the repo root)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run unit tests (must all pass before hardware testing)
ctest --test-dir build --output-on-failure
```

Ensure `test_ring_buffer` and `test_printer_manager` both pass before proceeding.

---

## 2. USB Access (udev Rule)

USB access requires a udev rule for non-root use. If you completed Phase 1 testing your rule is already in place. See **Phase 1 §2** for the full setup procedure.

Quick check:

```bash
ls -l /dev/bus/usb/$(lsusb | grep -i printer | awk '{print $2"/"$4}' | tr -d :) 2>/dev/null
# permissions should show rw-rw-rw-
```

---

## 3. Verify Detection

With the printer plugged in:

```bash
./build/tspl_print --list
```

Expected output:

```
Found 1 USB printer(s):
  [0] 09c6:0426 bus1 addr5 "AiYin LabelPrinter"
```

If two printers are connected:

```
Found 2 USB printer(s):
  [0] 09c6:0426 bus1 addr5 "AiYin LabelPrinter"
  [1] 03f0:0c17 bus1 addr7 "HP LaserJet P1005"
```

---

## 4. Test Assets

### 4.1 TSPL job file (`test_label.tspl`)

```
SIZE 101 mm,151 mm
GAP 3 mm,0 mm
DIRECTION 0
CLS
TEXT 10,10,"TSS24.BF2",0,1,1,"Phase 2 Test"
TEXT 10,40,"TSS24.BF2",0,1,1,"Auto-detect OK"
PRINT 1,1
```

### 4.2 PCL job file (`test_pcl.bin`) — optional, for multi-protocol test

```bash
python3 -c "
import sys
data  = b'\x1b\x45'           # PCL reset
data += b'\x1b&l0O'           # portrait
data += b'\x1b&l6D'           # 6 LPI
data += b'\x1b(10U'           # PC-8 symbol set
data += b'\x1b(s0p10h12v0s0b3T' # Courier 10 CPI
data += b'Phase 2 PCL Test\r\n'
data += b'\x0c'               # form feed
data += b'\x1b\x45'           # PCL reset
sys.stdout.buffer.write(data)
" > test_pcl.bin
```

### 4.3 ESC/P job file (`test_escp.bin`) — optional, for multi-protocol test

```bash
python3 -c "
import sys
data  = b'\x1b@'              # ESC/P init
data += b'\x1bx\x01'          # NLQ mode
data += b'\x1bP'              # 10 CPI
data += b'\x1b3\x24'          # line spacing (216/6=36=0x24)
data += b'Phase 2 ESC/P Test\r\n'
data += b'\x0c'               # form feed
sys.stdout.buffer.write(data)
" > test_escp.bin
```

---

## 5. Test Cases

Work through these in order.

---

### Test 5.1 — `--list` with printer plugged in

**Command:**
```bash
./build/tspl_print --list
```

**Expected:**
- At least one device listed with VID:PID, bus, address, and model name
- No errors

**Sample output:**
```
Found 1 USB printer(s):
  [0] 09c6:0426 bus1 addr5 "AiYin LabelPrinter"
```

---

### Test 5.2 — `--list` with printer unplugged

Unplug the printer, then:

```bash
./build/tspl_print --list
echo "Exit code: $?"
```

**Expected:**
```
No USB printers found.
Exit code: 0
```

No crash, no libusb error. Re-plug the printer before continuing.

---

### Test 5.3 — TSPL auto-detection via `--verbose`

```bash
./build/tspl_print --verbose test_label.tspl 2>&1 | grep -i "auto-detect"
```

**Expected:**
```
[INFO ][      0 ms][PrinterManager  ] Protocol auto-detected: TSPL
```

The protocol detection path calls `TsplHandler::probe()`, which matches the `SIZE` command at the start of the file.

---

### Test 5.4 — PCL auto-detection (requires PCL printer)

With a PCL printer connected:

```bash
./build/tspl_print --verbose test_pcl.bin 2>&1 | grep -i "auto-detect"
```

**Expected:**
```
[INFO ][      0 ms][PrinterManager  ] Protocol auto-detected: PCL
```

---

### Test 5.5 — ESC/P auto-detection (requires ESC/P printer)

With an ESC/P printer connected:

```bash
./build/tspl_print --verbose test_escp.bin 2>&1 | grep -i "auto-detect"
```

**Expected:**
```
[INFO ][      0 ms][PrinterManager  ] Protocol auto-detected: ESCP
```

---

### Test 5.6 — Unplug, re-plug, send job (DeviceRecord lifecycle)

This test verifies that each invocation discovers the printer fresh — no stale state from a previous run.

```bash
# Step 1: Send a job
./build/tspl_print test_label.tspl
echo "Step 1 exit: $?"

# Step 2: Unplug the printer, wait 2 seconds, re-plug
# (do this manually, then continue)

# Step 3: Send the same job again — should succeed without restarting binary
./build/tspl_print test_label.tspl
echo "Step 3 exit: $?"
```

**Expected:** Both invocations succeed (exit 0). Each opens the device, sends the job, and closes it independently.

---

### Test 5.7 — Cached protocol across two invocations (TSPL)

The `DeviceRecord` caches the detected protocol after the first successful job. Run the same job twice with `--verbose` and confirm the same protocol is reported:

```bash
./build/tspl_print --verbose test_label.tspl 2>&1 | grep -i "auto-detect"
./build/tspl_print --verbose test_label.tspl 2>&1 | grep -i "auto-detect"
```

**Expected:** Both invocations show `Protocol auto-detected: TSPL`. (Each invocation re-opens and re-detects, since the CLI process exits and does not retain device state between runs — the cache is per-process. This test confirms the probe path is reliable and consistent.)

---

### Test 5.8 — No printer connected: graceful failure

```bash
# Unplug all printers, then:
./build/tspl_print test_label.tspl
echo "Exit code: $?"
```

**Expected:**
```
Error: no USB printer found. Connect a printer or specify --vid/--pid.
Exit code: 1
```

---

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `--list` shows no device when printer is plugged in | udev rule missing or not applied | See Phase 1 §2; re-plug after reloading rules |
| `--list` shows device but job send fails | Kernel `usblp` driver claimed interface | `sudo rmmod usblp`; optionally blacklist (see Phase 1 §6) |
| `Protocol auto-detected: UNKNOWN` in `--verbose` | Job data format doesn't match any probe | Verify the file was generated correctly; check that the file starts with `SIZE`, `\x1b\x45`, or `\x1b@` |
| Second printer not listed | Not a printer-class USB device (interface class 0x07) | Some devices expose printer class only after enabling via driver — check `lsusb -v` for bInterfaceClass |
| Exit code 1 immediately after re-plug | Device node not yet ready | Wait 1 second after plugging before running the command |

---

## 7. Phase 2 Acceptance Checklist

Mark each item after verifying:

- [ ] **5.1** `--list` shows printer(s) when plugged in
- [ ] **5.2** `--list` prints "No USB printers found" when unplugged; exit 0
- [ ] **5.3** `--verbose` log shows "Protocol auto-detected: TSPL" for TSPL job
- [ ] **5.4** `--verbose` log shows "Protocol auto-detected: PCL" (if PCL printer available)
- [ ] **5.5** `--verbose` log shows "Protocol auto-detected: ESCP" (if ESC/P printer available)
- [ ] **5.6** Unplug → re-plug → second job succeeds without restarting binary (two separate invocations)
- [ ] **5.8** Graceful error with exit 1 when no printer connected
- [ ] Unit tests pass: `ctest --test-dir build --output-on-failure` (especially `test_ring_buffer`, `test_printer_manager`)

All items checked = Phase 2 complete.
