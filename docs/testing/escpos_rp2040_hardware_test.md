# ESC/POS on RP2040 Hardware Testing Guide

**Firmware personality:** `1028` (`EscposTextGenerator`, COL_40)
**Protocol:** ESC/POS — plain text, line-oriented, like Atari 820/822
**Target printer:** POS80 (`0483:5720`, STM32-based, USB printer class 07)
**Behavior:** Each Atari `LPRINT` line prints immediately as `text + LF`. No auto-cut.
User feeds paper and tears manually.

---

## 1. Prerequisites

### 1.1 Hardware

**Required:**
- Raspberry Pi Pico (RP2040) or compatible board
- POS80 80mm thermal receipt printer with paper roll loaded
- USB-A cable: RP2040 USB host port → printer (USB-A on both sides, or A-to-B if printer uses USB-B)
- USB Micro-B cable: host PC → RP2040 (flashing and power)
- Debug serial: USB-to-UART adapter (3.3 V logic) for reading RP2040 log output

**SIO connection to Atari (choose one):**

| Option A — Real Atari | Option B — USB-to-UART adapter (no Atari needed) |
|----------------------|--------------------------------------------------|
| Atari 400/800/XL/XE + SIO cable | USB-to-UART adapter, 3.3 V logic (CP2102, FT232RL, etc.) |
| SIO pin 3 (DATA in) → RP2040 pin 5 (UART1 RX) | Adapter TX → RP2040 pin 5 |
| SIO pin 5 (DATA out) → RP2040 pin 4 (UART1 TX) | Adapter RX → RP2040 pin 4 |
| SIO pin 7 (CMD) → RP2040 GPIO26 | Adapter RTS → RP2040 GPIO26 |
| SIO pin 4 (GND) → RP2040 GND | Adapter GND → RP2040 GND |

SIO connector pinout (15-pin DIN-style, as viewed from the rear of the Atari):

| Pin | Signal | RP2040 |
|-----|--------|--------|
| 3   | Serial in (Atari TX → device RX) | Pin 5 (UART1 RX) |
| 4   | GND | GND |
| 5   | Serial out (device TX → Atari RX) | Pin 4 (UART1 TX) |
| 7   | CMD (active-low) | GPIO26 |
| 10  | +5 V (optional — can power RP2040) | VSYS (via diode if sharing) |

> **Power note:** If powering the RP2040 from the Atari SIO +5 V (pin 10), do **not**
> connect the USB Micro-B power cable at the same time.

### 1.2 Software (build host)

```bash
# ARM toolchain
sudo apt install gcc-arm-none-eabi cmake ninja-build

# Verify pico-sdk is available
ls $PICO_SDK_PATH/CMakeLists.txt   # must exist

# Python + pyserial for SIO injection (Option B, no Atari)
pip3 install pyserial
```

---

## 2. Build the Firmware

### 2.1 Standard cmake build

```bash
# From the repo root
cmake -B build-rp2040 \
      -DTARGET_RP2040=ON \
      -DPICO_SDK_PATH=$PICO_SDK_PATH \
      -DATARI_PRINTER_PERSONALITY=1028

cmake --build build-rp2040 -j$(nproc)
```

UF2 binary location:
```
build-rp2040/usb_printer_rp2040.uf2
```

### 2.2 VS Code Pico extension build

Open `pico/usbprinter/` in VS Code. The extension automatically sets
`ATARI_PRINTER_PERSONALITY=1028` (already configured in `pico/usbprinter/CMakeLists.txt`).
Use the **Build** task or run:

```bash
./pico_debug.sh   # build + flash + open serial monitor
```

---

## 3. Flash the Firmware

1. Hold **BOOTSEL** on the Pico and plug in the USB cable (or press RESET while holding BOOTSEL).
2. The Pico appears as `RPI-RP2` mass-storage on the host PC.
3. Copy the UF2:
   ```bash
   cp build-rp2040/usb_printer_rp2040.uf2 /media/$USER/RPI-RP2/
   # or, if using pico_debug.sh, flashing happens automatically
   ```
4. The Pico reboots and starts the firmware immediately.

---

## 4. Verify Firmware Boot

Connect a serial terminal to the RP2040 UART debug output (115200 baud, 8N1).
The debug UART is on pin 1 (TX) of the Pico — connect your USB-to-UART adapter
RX to this pin.

```bash
# Find the port
ls /dev/ttyUSB*   # or /dev/ttyACM* if using picoprobe

# Connect
screen /dev/ttyUSB0 115200
# or: minicom -b 115200 -D /dev/ttyUSB0
```

Expected boot output:
```
[INFO ][      0 ms][main            ] USB Printer Driver RP2040 starting
[INFO ][      0 ms][PrinterManager  ] Registered handler: TSPL
[INFO ][      0 ms][PrinterManager  ] Registered handler: ESC/P
[INFO ][      0 ms][PrinterManager  ] Registered handler: PCL
[INFO ][      0 ms][main            ] Boot: no NVM config in slot 0, using compiled defaults
[INFO ][      0 ms][main            ] Entering main loop
```

---

## 5. Connect the Printer

Plug the POS80 USB cable into the RP2040 USB host port.

Expected log output within ~1 second:
```
[INFO ][    520 ms][main            ] USB device mounted: addr=1
[INFO ][    540 ms][main            ] addr=1: printer ready (epOut=0x01)
[INFO ][    540 ms][PrinterManager  ] Device opened: 0483:5720 bus1 addr1 "POS80"
```

> If you see `addr=1: no printer-class interface with BULK OUT`, the printer
> was not recognised as USB class 07. Verify the printer is powered on and
> the cable is good; try a different USB port on the RP2040 hub if applicable.

### 5.1 Printer self-test (baseline)

Before any Atari testing, verify the printer works on its own:
hold the **Feed** button on the printer while powering it on. It should print a
self-test page with font samples and a status line. If it doesn't, check the
paper roll is loaded correctly.

---

## 6. Test Cases

Work through these in order. Each test uses the Atari `BASIC` prompt or the
`inject_sio.py` script (§7) if no Atari is available.

---

### Test 6.1 — Single line prints immediately

**On Atari BASIC:**
```
OPEN #1,8,0,"P:": ? #1,"HELLO WORLD": CLOSE #1
```
Or equivalently:
```
LPRINT "HELLO WORLD"
```

**Expected:**
- RP2040 log:
  ```
  [INFO ][   ...ms][SioPrinterEmulator] SIO dev=0x40 cmd=WRITE aux1=0x00 aux2=0x00
  [INFO ][   ...ms][SioPrinterEmulator] SIO data: "HELLO WORLD             ..."
  [INFO ][   ...ms][PrinterManager  ] Protocol auto-detected: ESC/P
  ```
- Printer immediately advances paper and prints:
  ```
  HELLO WORLD
  ```
- No cut, no extra feed — just the text line. Paper sits at print head.

---

### Test 6.2 — Multiple lines print separately

```
LPRINT "LINE ONE"
LPRINT "LINE TWO"
LPRINT "LINE THREE"
```

**Expected:** Each `LPRINT` triggers an independent USB write. The printer prints
each line as it arrives:
```
LINE ONE
LINE TWO
LINE THREE
```

The paper advances one line at a time. After the last line, tear the paper manually.

---

### Test 6.3 — Full-width line (40 characters)

```
LPRINT "1234567890123456789012345678901234567890"
```

**Expected:** All 40 characters print on a single line without wrapping.
The 80mm paper at default font gives approximately 42 characters per line,
so 40 characters fits cleanly.

---

### Test 6.4 — Atari BASIC program (list printout)

Enter and run this short program:

```
10 REM ESCPOS TEST
20 FOR I=1 TO 5
30 LPRINT "LINE ";I;" OF 5"
40 NEXT I
50 LPRINT "DONE"
```

**Expected output on paper:**
```
LINE  1 OF 5
LINE  2 OF 5
LINE  3 OF 5
LINE  4 OF 5
LINE  5 OF 5
DONE
```

Verify there are no missing lines and no duplicate lines. Tear the receipt.

---

### Test 6.5 — LLIST (program listing to printer)

At the `BASIC` prompt after entering the program from 6.4:

```
LLIST
```

**Expected:** Each line of the BASIC program prints as a separate receipt line,
in program-number order. The RP2040 log should show one SIO `WRITE` frame per
program line.

---

### Test 6.6 — Blank line handling

```
LPRINT "TOP"
LPRINT ""
LPRINT "BOTTOM"
```

**Expected:** A blank line (just a LF) advances the paper one line. Output:
```
TOP

BOTTOM
```

Three separate USB writes occur; the blank `LPRINT ""` sends a single `0x0A`.

---

### Test 6.7 — GPIO 8 button (no-op for ESCPOS)

Press the **GPIO 8** button (if wired).

**Expected:** The button calls `generator->flush()`. For the ESCPOS generator,
`flush()` returns an empty job (each line was already auto-flushed), so nothing
is submitted and no extra paper is fed. The RP2040 log should show:
```
[INFO ][   ...ms][main            ] Button: label submitted
```
but nothing prints (empty job guard: `if (!job.rawData.empty())`).

This is correct — the button is a no-op for line-oriented ESC/POS mode.

---

### Test 6.8 — Printer reconnect

1. Unplug the POS80 USB cable while the firmware is running.
2. RP2040 log:
   ```
   [INFO ][   ...ms][main            ] USB device unmounted: addr=1
   ```
3. Send a few `LPRINT` lines from the Atari — they will queue in the generator
   but submit will fail (no active device).
4. Re-plug the printer.
5. RP2040 log shows `addr=2: printer ready` (address increments on re-plug).
6. Send another `LPRINT` — it should print successfully.

---

### Test 6.9 — SIO timing: no Error 138

Run a tight BASIC loop:
```
10 FOR I=1 TO 20: LPRINT "LINE ";I: NEXT I
```

**Expected:** All 20 lines print without the Atari reporting `ERROR 138`
(device timeout). Each USB write (~37 ms) completes before the next SIO
WRITE cycle begins (~68 ms at 19200 baud). Monitor the RP2040 log — there
should be no `timeout` or `NAK` messages.

---

## 7. Testing Without an Atari (`inject_sio.py`)

If no Atari is available, use this script to inject SIO printer write frames
via a USB-to-UART adapter (Option B wiring from §1.1).

```python
#!/usr/bin/env python3
# inject_sio.py — Send Atari SIO printer write frames from a PC
# Usage: python3 inject_sio.py /dev/ttyUSB0 "TEXT TO PRINT"

import serial, time, sys

def sio_checksum(data: bytes) -> int:
    s = 0
    for b in data:
        s += b
        if s >= 256:
            s -= 255
    return s & 0xFF

def make_record(text: str, size: int = 40) -> bytes:
    rec = bytearray(b' ' * size)
    enc = text.encode('ascii')
    rec[:len(enc)] = enc[:size]
    if len(enc) < size:
        rec[len(enc)] = 0x9B  # ATASCII EOL
    return bytes(rec)

def send_line(port: serial.Serial, text: str) -> None:
    port.rts = False        # CMD asserted (active-low)
    time.sleep(0.001)
    frame = bytearray([0x40, 0x57, 0x00, 0x00])
    frame.append(sio_checksum(frame))
    port.write(bytes(frame))
    port.rts = True         # CMD deasserted
    time.sleep(0.001)

    ack = port.read(1)
    if ack != b'A':
        raise RuntimeError(f"CMD ACK expected, got {ack!r}")

    record = make_record(text)
    port.write(record + bytes([sio_checksum(record)]))
    time.sleep(0.002)

    ack2 = port.read(1)
    if ack2 != b'A':
        raise RuntimeError(f"Data ACK expected, got {ack2!r}")

    complete = port.read(1)
    print(f"  Sent: {text!r}  → COMPLETE={complete!r}")

if __name__ == '__main__':
    dev, text = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "HELLO"
    with serial.Serial(dev, 19200, timeout=1) as p:
        p.rts = True
        time.sleep(0.1)
        send_line(p, text)
```

**Usage:**
```bash
# Single line
python3 inject_sio.py /dev/ttyUSB0 "HELLO FROM PC"

# Multiple lines (shell loop)
for line in "RECEIPT" "----------" "Item 1  $5.00" "Item 2  $3.50" "TOTAL   $8.50"; do
    python3 inject_sio.py /dev/ttyUSB0 "$line"
done
```

---

## 8. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No boot log on serial | Wrong UART pin or baud rate | Confirm debug TX is pin 1; baud 115200 |
| `no printer-class interface with BULK OUT` | Printer not recognised as class 07 | Verify printer is powered; check USB cable |
| SIO frames not received | Wiring error or wrong UART | Verify pin 5 = UART1 RX, pin 4 = UART1 TX, GPIO26 = CMD |
| `ERROR 138` on Atari | USB write blocked tick() for too long | Unlikely at 37ms/68ms; check for other delays in the loop |
| Lines missing or duplicated | SIO checksum errors causing NAK/retry | Check SIO cable quality; watch for `Bad record checksum` in log |
| Text garbled on paper | Printer default codepage is not ASCII | Unlikely for plain ASCII; no codepage init is sent |
| Nothing prints after re-plug | PrinterManager not re-opening device | Check log for `addr=N: printer ready`; subsequent LPRINT should recover |
| Blank `LPRINT ""` feeds extra lines | Each blank `writeBlank()` sends one LF | Expected — this is correct 820-style behavior |

### 8.1 Enable debug logging

If the INFO log is not enough, set the log level to DEBUG in `main_rp2040.cpp`:
```cpp
Logger::setLevel(LogLevel::DEBUG);
```
Rebuild and flash. `[DEBUG]` lines show each byte of SIO frame processing.

---

## 9. Personality Reference

| Personality | Value | Generator | Column mode | Auto-flush |
|-------------|-------|-----------|-------------|------------|
| TSPL label  | `TSPL` | `TsplTextGenerator` | COL_40 | Button or ESC~P |
| ESC/P dot-matrix | `825` / `1027` | `EscpTextGenerator` | COL_40 | Button or ESC~P |
| PCL laser/inkjet | `1025` | `PclTextGenerator` | COL_80 | Button or ESC~P |
| **ESC/POS thermal** | **`1028`** | **`EscposTextGenerator`** | **COL_40** | **Per line (immediate)** |

To switch back to TSPL, change `ATARI_PRINTER_PERSONALITY=1028` →
`ATARI_PRINTER_PERSONALITY=TSPL` in `pico/usbprinter/CMakeLists.txt` and rebuild.

---

## 10. Acceptance Checklist

- [ ] Firmware boots and UART log shows all three handlers registered
- [ ] POS80 enumerates: `addr=1: printer ready (epOut=0x01)` in log
- [ ] **6.1** Single `LPRINT` prints immediately, no cut
- [ ] **6.2** Three `LPRINT` lines each print as separate lines on the paper
- [ ] **6.3** 40-character line fits on 80mm paper without wrapping
- [ ] **6.4** BASIC program loop: all 6 lines print in order
- [ ] **6.5** `LLIST` prints program lines one per line
- [ ] **6.6** `LPRINT ""` advances one blank line
- [ ] **6.7** GPIO 8 button press produces no spurious output (empty flush)
- [ ] **6.8** Printer reconnect: prints correctly after re-plug
- [ ] **6.9** 20-line loop completes without Atari `ERROR 138`
- [ ] `ctest --test-dir build --output-on-failure` — all 13 tests pass

All items checked = ESC/POS RP2040 hardware test complete.
