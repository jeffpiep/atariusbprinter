# Phase 1 Hardware Testing Guide

**Binary:** `build/tspl_print`
**Protocol:** TSPL (label printers — Zebra, TSC, Godex, and ZPL-compatible devices)
**Requirement:** A USB-connected label printer that accepts TSPL or ZPL commands.

---

## 1. Prerequisites

### 1.1 Hardware

- A TSPL or ZPL-compatible label printer connected via USB (e.g. Zebra ZD421, ZD621, ZD230; TSC TTP-244 series; Godex EZ-series; HPRT or iDPRT ZPL printers)
- Labels loaded and media calibrated (printer self-test should produce a label)
- USB cable connecting printer to the Linux host

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

## 2. USB Access (udev Rule)

By default, Linux requires root to access USB devices. Add a udev rule so you can run `tspl_print` as a normal user.

### Step 1 — Find your printer's VID:PID

```bash
lsusb
```

Look for your printer model. Actual output:

```
Bus 003 Device 019: ID 09c6:0426 AiYin LabelPrinter Printer
```

The VID is `09c6`, the PID is `0426`.

### Step 2 — Create the udev rule

```bash
sudo tee /etc/udev/rules.d/99-tspl-printer.rules <<'EOF'
# Allow non-root USB access to label printers
SUBSYSTEM=="usb", ATTR{idVendor}=="09c6", MODE="0666"
EOF
```

Replace `09c6` with your printer's VID. To cover all printer-class devices instead:

```bash
sudo tee /etc/udev/rules.d/99-tspl-printer.rules <<'EOF'
SUBSYSTEM=="usb", ENV{ID_USB_INTERFACES}=="*:070100:*", MODE="0666"
EOF
```

### Step 3 — Reload rules and re-plug

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Unplug and re-plug the printer USB cable.

---

## 3. Verify the Printer is Detected

```bash
./build/tspl_print --list
```

Expected output (your VID:PID and model will differ):

```
Found 1 USB printer(s):
  [0] 09c6:0426 bus1 addr5 "Zebra Technologies ZTC ZD421"
```

**If "No USB printers found":**
- Check `lsusb` to confirm the device is visible to the OS
- Check udev rule is applied (`ls -l /dev/bus/usb/001/005` — permissions should show `rw-rw-rw-`)
- Try running with `sudo ./build/tspl_print --list` to rule out a permission issue

**If "Error: failed to enumerate USB devices":**
- libusb context failed to initialise — check `dmesg | tail` for USB errors

---

## 4. Create a Test TSPL Job File

Save this as `test_label.tspl`:

```
SIZE 60 mm,30 mm
GAP 3 mm,0 mm
DIRECTION 0
CLS
TEXT 10,10,"TSS24.BF2",0,1,1,"Phase 1 Test"
TEXT 10,40,"TSS24.BF2",0,1,1,"USB Printer Driver"
TEXT 10,70,"TSS24.BF2",0,1,1,"TSPL OK"
PRINT 1,1
```

For a 4"×2" (102 mm × 51 mm) label, use instead:

```
SIZE 102 mm,51 mm
GAP 3 mm,0 mm
DIRECTION 0
CLS
TEXT 20,20,"TSS24.BF2",0,2,2,"Phase 1 Test"
TEXT 20,80,"TSS24.BF2",0,1,1,"USB Printer Driver OK"
PRINT 1,1
```

> **Note on units:** `SIZE` uses mm. Font `TSS24.BF2` is a built-in 24-dot font. The `TEXT` command places text at (x, y) in dot coordinates (1 dot = 1/8 mm at 203 dpi). If text is clipped, reduce the y-coordinates or increase the label height.

---

## 5. Test Cases

Work through these in order. Each builds on the previous.

---

### Test 5.1 — Auto-detect printer and send a job from a file

**Command:**
```bash
./build/tspl_print test_label.tspl
```

**Expected:**
- No error output to terminal
- `[INFO]` lines appear on stderr confirming device opened and job sent
- A label is printed with the three text lines

**Sample successful output:**
```
[INFO ][      0 ms][LinuxUsbTransport] Claimed interface 0 (epOut=0x01 epIn=0x82 hasEpIn=1)
[INFO ][      0 ms][LinuxUsbTransport] Opened 09c6:0426 bus1 addr5 "Zebra Technologies ZTC ZD421"
[INFO ][      0 ms][PrinterManager  ] Device opened: 09c6:0426 bus1 addr5 "Zebra Technologies ZTC ZD421"
[INFO ][      0 ms][PrinterManager  ] Protocol auto-detected: TSPL
[INFO ][      0 ms][TsplHandler     ] sendJob 'test_label.tspl': 138 bytes in chunks of 4096
[INFO ][    100 ms][TsplHandler     ] sendJob 'test_label.tspl': complete
[INFO ][    100 ms][main            ] Job completed successfully
```

---

### Test 5.2 — Specify VID/PID explicitly

Use the VID and PID from `--list`:

```bash
./build/tspl_print --vid 09c6 --pid 0426 test_label.tspl
```

Expected: same result as 5.1. This verifies the explicit device selection path.

---

### Test 5.3 — Send a job via stdin

```bash
cat test_label.tspl | ./build/tspl_print -
```

Or pipe directly:

```bash
printf 'SIZE 60 mm,30 mm\r\nGAP 3 mm,0 mm\r\nDIRECTION 0\r\nCLS\r\nTEXT 10,10,"TSS24.BF2",0,1,1,"stdin test"\r\nPRINT 1,1\r\n' \
  | ./build/tspl_print -
```

Expected: label printed with "stdin test" text.

---

### Test 5.4 — Debug / verbose output

```bash
./build/tspl_print --verbose test_label.tspl
```

Expected: additional `[DEBUG]` lines appear. Useful for diagnosing USB endpoint discovery.

---

### Test 5.5 — Graceful error: file not found

```bash
./build/tspl_print /nonexistent.tspl
echo "Exit code: $?"
```

Expected:
```
Error: cannot read file '/nonexistent.tspl'
Exit code: 1
```
No crash, no libusb error, clean exit code 1.

---

### Test 5.6 — Graceful error: no printer connected

Unplug the printer, then run:

```bash
./build/tspl_print test_label.tspl
echo "Exit code: $?"
```

Expected:
```
Error: no USB printer found. Connect a printer or specify --vid/--pid.
Exit code: 1
```

---

### Test 5.7 — Graceful error: wrong VID/PID

```bash
./build/tspl_print --vid dead --pid beef test_label.tspl
echo "Exit code: $?"
```

Expected:
```
[ERROR][      0 ms][LinuxUsbTransport] Device dead:beef not found or cannot be opened
[ERROR][      0 ms][PrinterManager  ] Failed to open device dead:beef bus0 addr0 " "
Error: failed to open printer dead:beef bus0 addr0 " "
Exit code: 1
```

---

### Test 5.8 — Large job (chunking verification)

Generate a large TSPL file to verify 4096-byte chunk splitting:

```bash
python3 -c "
lines = ['SIZE 102 mm,151 mm\r\n', 'GAP 3 mm,0 mm\r\n', 'DIRECTION 0\r\n', 'CLS\r\n']
for i in range(50):
    lines.append(f'TEXT 10,{10+i*20},\"TSS24.BF2\",0,1,1,\"Line {i+1} of 50\"\r\n')
lines.append('PRINT 1,1\r\n')
print(''.join(lines), end='')
" > big_label.tspl

wc -c big_label.tspl    # should be well over 4096 bytes
./build/tspl_print --verbose big_label.tspl
```

Expected: `[INFO]` shows multiple chunk writes (each ≤ 4096 bytes). The printer prints one label (TSPL `PRINT 1,1` produces one copy regardless of job size).

---

### Test 5.9 — Query printer status (if printer supports it)

```bash
./build/tspl_print --verbose test_label.tspl 2>&1 | grep -i status
```

If the printer has a bidirectional USB channel, you may see a `queryStatus` debug line with the raw `~HS` response. Many label printers are write-only; a "no response (write-only device?)" message at DEBUG level is normal and non-fatal.

---

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `Device not found or cannot be opened` | Permission denied | Add udev rule (§2), re-plug |
| `Device not found or cannot be opened` | Wrong VID:PID in `--vid/--pid` | Run `--list` to confirm IDs |
| `No printer-class interface found` | Printer not in print mode | Power-cycle; ensure driver not loaded (`sudo rmmod usblp`) |
| `write failed` / `Disconnected` | USB cable issue | Try a different cable/port |
| Printer receives job but prints blank label | TSPL SIZE doesn't match loaded media | Adjust `SIZE` command to match physical label width/height |
| Printer beeps / error light | Label calibration needed | Hold Feed button 3 sec for gap calibration; see printer manual |
| Text truncated on label | Y coordinate too high | Reduce y values in `TEXT` commands; all content must fit within `SIZE` height |

### Kernel driver conflict

If libusb cannot claim the interface, the kernel `usblp` driver may be loaded:

```bash
sudo rmmod usblp
./build/tspl_print test_label.tspl
```

To prevent `usblp` loading permanently:

```bash
echo "blacklist usblp" | sudo tee /etc/modprobe.d/blacklist-usblp.conf
sudo update-initramfs -u
```

---

## 7. Phase 1 Acceptance Checklist

Mark each item after verifying:

- [ ] **5.1** Label printed from a TSPL file (auto-detect)
- [ ] **5.2** Label printed using explicit `--vid`/`--pid`
- [ ] **5.3** Label printed via stdin (`-`)
- [ ] **5.5** Graceful error, exit 1 when file not found
- [ ] **5.6** Graceful error, exit 1 when no printer connected
- [ ] **5.7** Graceful error, exit 1 with wrong VID:PID
- [ ] **5.8** Large job (>4 KB) splits into chunks, prints correctly
- [ ] Unit tests pass: `ctest --test-dir build --output-on-failure`

All items checked = Phase 1 complete.
