# ESC/POS Hardware Testing Guide

**Binary:** `build/tspl_print`
**Protocol:** ESC/POS (thermal receipt printers — 80mm roll, Epson TM-series compatible)
**Device under test:** `0483:5720` POS80 (STM32-based, USB printer class 07)

---

## 1. Prerequisites

### 1.1 Hardware

- 80mm thermal receipt printer connected via USB
- Paper roll loaded (self-test via front-panel button should produce a receipt)
- USB cable connecting printer to Linux host

### 1.2 Software

```bash
# Ensure libusb is installed
dpkg -l libusb-1.0-0 | grep -E "^ii"

# Build the project (from the repo root)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

The binary is at `build/tspl_print`.

---

## 2. Identify the Device

```bash
lsusb | grep -i 0483
```

Expected:

```
Bus 003 Device 121: ID 0483:5720 STMicroelectronics Mass Storage Device
```

> `lsusb` shows the generic STM32 chip label ("Mass Storage Device") at the device
> level. The actual product string reported by the printer firmware is **"POS80"**,
> visible via `lsusb -v` (`iProduct 2 POS80`).

### 2.1 Check the actual USB interface class

```bash
lsusb -v -d 0483:5720 2>/dev/null | grep -E "bInterfaceClass|bDeviceClass|idProduct|idVendor|iProduct"
```

Confirmed output for the POS80:

```
  bDeviceClass            0 [unknown]
  idVendor           0x0483 STMicroelectronics
  idProduct          0x5720 Mass Storage Device
  iProduct                2 POS80
      bInterfaceClass         7 Printer   ✓
```

**Result: `bInterfaceClass 7 Printer`** — `claimPrinterInterface()` accepts this
device as-is. No code changes needed. The §7.1 workaround does not apply.

---

## 3. USB Access (udev Rule)

### Step 1 — Add udev rule for the STMicroelectronics VID

```bash
sudo tee /etc/udev/rules.d/99-escpos-printer.rules <<'EOF'
# Allow non-root USB access to STM32-based receipt printers
SUBSYSTEM=="usb", ATTR{idVendor}=="0483", MODE="0666"
EOF
```

### Step 2 — Reload and re-plug

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and re-plug the USB cable. Check the device ID - it changed from 121 to 122
Verify permissions:

```bash
# Find the device node (bus and device number from lsusb output)
ls -l /dev/bus/usb/003/122
# Should show: crw-rw-rw- (world-readable/writable)
```

---

## 4. Verify the Printer is Detected

```bash
./build/tspl_print --list
```

Expected output:

```
Found 1 USB printer(s):
  [0] 0483:5720 bus3 addr122 "POS80"
```

> The `addr` number increments each time the device is re-plugged — this is normal.
> Your number will differ; only the VID:PID matters for `--vid/--pid` targeting.

The POS80 is USB printer class 07 so it enumerates normally. If you see
"No USB printers found", check that the udev rule is applied and the cable
is plugged in (`lsusb` should still show the device).

---

## 5. Test Cases

Work through these in order.

---

### Test 5.1 — Built-in ESC/POS test receipt (auto-detect)

```bash
./build/tspl_print --escpos-test
```

**Expected:**
- `[INFO]` lines on stderr confirming device opened and bytes written
- An 80mm test receipt prints with header, item list, totals, and partial cut

**Sample successful output:**
```
[INFO ][      0 ms][LinuxUsbTransport] Claimed interface 0 (epOut=0x01 epIn=0x81 hasEpIn=1)
[INFO ][      0 ms][LinuxUsbTransport] Opened 0483:5720 bus3 addr122 "POS80"
[INFO ][      0 ms][main            ] ESC/POS test job: 312 bytes
[INFO ][     85 ms][main            ] ESC/POS test receipt sent successfully
```

**Printed receipt should show:**
```
   RECEIPT PRINTER TEST      ← double-height/width, bold
   80mm ESC/POS device test
   --------------------------------
   Item                      Price
   --------------------------------
   Widget A                 $10.00
   ...
   TOTAL                    $65.86  ← double-size, bold
   ================================
      Thank you for your purchase!
```

---

### Test 5.2 — Explicit VID/PID

```bash
./build/tspl_print --vid 0483 --pid 5720 --escpos-test
```

Expected: same receipt as 5.1. Confirms the explicit device selection path works
when `--list` returns no printers.

---

### Test 5.3 — Verbose output

```bash
./build/tspl_print --vid 0483 --pid 5720 --escpos-test --verbose
```

Expected: `[DEBUG]` lines showing USB endpoint discovery, interface number, and
each write call. Useful for diagnosing interface-class or endpoint issues.

---

### Test 5.4 — Raw ESC/POS file via stdin

Build a minimal raw receipt and pipe it in:

```bash
printf '\x1b\x40\x1b\x61\x01Hello from stdin\x0a\x1b\x64\x04\x1d\x56\x42\x00' \
  | ./build/tspl_print --vid 0483 --pid 5720 -
```

Expected: short receipt prints with "Hello from stdin" and partial cut.

> **Note:** stdin mode goes through the `PrinterManager` and protocol handler
> chain. For raw ESC/POS data, the `EscpHandler` probe may match (ESC sequences
> are similar to ESC/P) or fall back to PCL raw pass-through. If the job is
> rejected by all handlers, use `--escpos-test` or write a raw file instead.

---

### Test 5.5 — Graceful error: no printer

Unplug the printer, then:

```bash
./build/tspl_print --escpos-test
echo "Exit code: $?"
```

Expected:
```
Error: no USB printer found. Use --vid/--pid to specify the receipt printer.
Exit code: 1
```

---

### Test 5.6 — Graceful error: wrong VID/PID

```bash
./build/tspl_print --vid dead --pid beef --escpos-test
echo "Exit code: $?"
```

Expected:
```
[ERROR][      0 ms][LinuxUsbTransport] Device dead:beef not found or cannot be opened
Error: failed to open printer dead:beef bus0 addr0 " "
Exit code: 1
```

---

### Test 5.7 — Paper cut variants

If the default partial cut (`GS V 66 0`) doesn't cut on your printer model, test
a full cut:

```bash
printf '\x1b\x40Hello - full cut test\x0a\x1b\x64\x04\x1d\x56\x00' \
  | ./build/tspl_print --vid 0483 --pid 5720 -
```

And a feed-only (no cut, for printers without cutter):

```bash
printf '\x1b\x40Hello - no cut test\x0a\x1b\x64\x08' \
  | ./build/tspl_print --vid 0483 --pid 5720 -
```

---

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `No USB printer found` with `--escpos-test` | udev rule missing | Add rule (§3), re-plug |
| `No printer-class interface found` | Device uses CDC/vendor-class USB | See §7.1 |
| `write failed` / timeout | USB cable or buffer issue | Try different USB port; add `--verbose` |
| Printer powers on but nothing prints | ESC/POS not the right protocol | Try printer self-test (hold Feed); check docs |
| Garbled text / wrong column width | Firmware codepage mismatch | Send `ESC t 0` (PC437) at start; see §7.2 |
| No cut at end of receipt | Cutter not present or different cut command | See Test 5.7 |
| Receipt prints but `--list` shows nothing | Not USB class 07 | Normal; use `--vid/--pid` explicitly |

### 6.1 Kernel driver conflict (`usblp`)

If `claimPrinterInterface()` succeeds but the interface cannot be claimed:

```bash
sudo rmmod usblp
./build/tspl_print --vid 0483 --pid 5720 --escpos-test
```

To prevent `usblp` from loading permanently:

```bash
echo "blacklist usblp" | sudo tee /etc/modprobe.d/blacklist-usblp.conf
sudo update-initramfs -u
```

---

## 7. Known Issues and Workarounds

### 7.1 Device uses non-printer USB class (CDC, vendor-specific)

> **Not applicable to the 0483:5720 POS80** — confirmed `bInterfaceClass 7 Printer`.
> This section applies to other receipt printers that use a different USB class.

If `lsusb -v` shows `bInterfaceClass 2 Communications` or `255 Vendor Specific`,
`claimPrinterInterface()` will reject it because it only accepts class `0x07`.

**Diagnosis:**

```bash
lsusb -v -d 0483:5720 2>/dev/null | grep -A3 "Interface Descriptor"
```

**Workaround — relax the interface class filter in `LinuxUsbTransport.cpp`:**

Find `claimPrinterInterface()` and change:

```cpp
// Before:
if (desc.bInterfaceClass != USB_CLASS_PRINTER) continue;

// After (also accept CDC and vendor-specific):
if (desc.bInterfaceClass != USB_CLASS_PRINTER &&
    desc.bInterfaceClass != 0x02 &&   // CDC
    desc.bInterfaceClass != 0xFF) continue;
```

Rebuild and retest. Limit this change to non-production/test builds — it will
attempt to claim any interface that has bulk OUT/IN endpoints, which may be wrong
for composite devices with multiple interfaces.

### 7.2 Codepage / character set

Some printers default to a non-ASCII codepage. If text prints with wrong
characters, prepend `ESC t 0` (select PC437) to the job:

```bash
printf '\x1b\x40\x1b\x74\x00Your text here\x0a\x1b\x64\x04\x1d\x56\x42\x00' \
  | ./build/tspl_print --vid 0483 --pid 5720 -
```

### 7.3 USB reconnect after first job

Some cheap receipt printers disconnect and re-enumerate after receiving the cut
command. This is normal — the device will reappear in `lsusb` within a second.
The next `--escpos-test` call will reopen it normally.

---

## 8. Acceptance Checklist

Mark each item after verifying:

- [X] USB class identified (`lsusb -v -d 0483:5720`) and udev rule applied
- [X] **5.1** Test receipt prints from `--escpos-test` (auto-detect or `--vid/--pid`)
- [X] **5.2** Test receipt prints with explicit `--vid 0483 --pid 5720`
- [X] **5.3** Raw ESC/POS bytes sent via stdin print correctly
- [X] **5.5** Graceful error, exit 1 when no printer connected
- [X] **5.6** Graceful error, exit 1 with wrong VID:PID
- [X] Printer auto-cuts (or feeds correctly if no cutter)
- [X] `ctest --test-dir build --output-on-failure` — all green

All items checked = ESC/POS hardware test complete.
