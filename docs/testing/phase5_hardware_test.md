# Phase 5 Hardware Testing Guide

**Firmware:** `build_rp2040/platform/rp2040/atariusbprinter.uf2`
**Focus:** Atari SIO printer emulator on RP2040 — LPRINT, 80-column join, TSPL idle-timeout flush, bad-checksum NAK
**Requirement:** RP2040 board + USB printer. Atari 8-bit computer is **optional** — see §4 for the `inject_sio.py` software substitute.

---

## 1. Prerequisites

### 1.1 Hardware

**Required:**
- Raspberry Pi Pico (RP2040) or compatible board
- USB printer (type must match the compiled personality — see §1.3)
- USB cable: host PC → RP2040 (for flashing and power)
- USB cable: RP2040 USB host port → printer

**SIO connection (choose one):**

| Option A — Atari computer | Option B — USB-to-UART adapter (no Atari needed) |
|--------------------------|--------------------------------------------------|
| Atari 400/800/XL/XE + SIO cable | USB-to-UART adapter (3.3 V logic, e.g. CP2102, FT232RL) |
| Wire SIO pins to RP2040 UART1 (pins 4/5) | Adapter TX → RP2040 pin 5 (UART1 RX); adapter RX → RP2040 pin 4 (UART1 TX) |
| SIO CMD line to RP2040 GPIO26 | Adapter RTS → RP2040 GPIO26 (active-low CMD line) |
| SIO GND to RP2040 GND | Adapter GND → RP2040 GND |

SIO connector pinout (15-pin DIN-style):

| Pin | Signal | RP2040 |
|-----|--------|--------|
| 1   | Clock in (unused for printer) | — |
| 2   | Clock out (unused) | — |
| 3   | DATA in (host→device) | Pin 5 (UART1 RX) |
| 4   | GND | GND |
| 5   | DATA out (device→host) | Pin 4 (UART1 TX) |
| 7   | CMD | GPIO26 |
| 10  | +5 V (if powering from Atari) | — |

> If powering the RP2040 from the Atari SIO +5 V, **do not** connect the USB power cable simultaneously.

### 1.2 Software (build host)

```bash
# Install ARM toolchain if not already present
sudo apt install gcc-arm-none-eabi cmake ninja-build

# Verify pico-sdk is available
ls $PICO_SDK_PATH/CMakeLists.txt   # should exist

# Python + pyserial for SIO injection (Option B)
pip3 install pyserial
```

### 1.3 Personality selection

The personality is set at compile time via `ATARI_PRINTER_PERSONALITY`:

| Personality | Generator | Column mode | Target printer |
|-------------|-----------|-------------|----------------|
| `825`       | `EscpTextGenerator` | COL_40 | ESC/P receipt or dot-matrix |
| `1025`      | `PclTextGenerator`  | COL_80 | PCL laser or inkjet |
| `1027`      | `EscpTextGenerator` | COL_40 | ESC/P printer (same as 825) |
| `TSPL`      | `TsplTextGenerator` | COL_40 + idle-timeout flush | TSPL label printer |

---

## 2. RP2040 Build

```bash
# From the repo root
cmake -B build_rp2040 \
      -DTARGET_RP2040=ON \
      -DPICO_SDK_PATH=/path/to/pico-sdk \
      -DATARI_PRINTER_PERSONALITY=825

cmake --build build_rp2040 -j$(nproc)
```

Replace `825` with `1025`, `1027`, or `TSPL` as needed.

The UF2 binary is at:
```
build_rp2040/platform/rp2040/atariusbprinter.uf2
```

### Flash procedure

1. Hold the **BOOTSEL** button on the Pico and plug in the USB cable (or press RESET while holding BOOTSEL).
2. The Pico appears as a USB mass-storage device named `RPI-RP2`.
3. Copy the UF2:
   ```bash
   cp build_rp2040/platform/rp2040/atariusbprinter.uf2 /media/$USER/RPI-RP2/
   ```
4. The Pico reboots automatically and begins running the firmware.

---

## 3. Verify Detection

After flashing, the RP2040 initialises the USB host and SIO UART. Connect the USB printer to the RP2040's USB host port. The LED (if present) should indicate printer enumeration.

To confirm the printer was enumerated, connect a serial terminal to the RP2040's USB CDC debug port (115200 baud):

```bash
# Find the CDC port
ls /dev/ttyACM*    # or /dev/ttyUSB*

# Connect (Ctrl-A then K to quit)
screen /dev/ttyACM0 115200
```

Expected debug output after boot:
```
[INFO ][    500 ms][Rp2040UsbTransport] Printer enumerated: 09c6:0426 bus1 addr1 "AiYin LabelPrinter"
[INFO ][    500 ms][PrinterManager  ] Device opened: 09c6:0426 bus1 addr1 "AiYin LabelPrinter"
[INFO ][    500 ms][SioPrinterEmulator] Ready — personality 825
```

---

## 4. Test Assets: SIO Frame Injector (`inject_sio.py`)

If you do not have an Atari computer, use this script to inject SIO printer write frames over a USB-to-UART adapter. The script asserts the CMD line via the adapter's RTS signal.

> For Option B wiring, check that your adapter can drive RTS and that GPIO26 on the RP2040 is pulled up internally (it is, by default in `SioUart::init()`).

```python
#!/usr/bin/env python3
# inject_sio.py — Sends a valid Atari printer write frame + 40-byte data record
# Usage: python3 inject_sio.py /dev/ttyUSB0 "HELLO WORLD"

import serial, struct, time, sys

def sio_checksum(data: bytes) -> int:
    """Atari SIO checksum: sum-of-bytes mod 256 with add-back carry."""
    s = 0
    for b in data:
        s += b
        if s >= 256:
            s -= 255
    return s & 0xFF

def make_record(text: str, eol: int = 0x9B, size: int = 40) -> bytes:
    """Build a 40-byte SIO record: text + $9B + space padding."""
    encoded = text.encode('ascii')
    rec = bytearray(size)
    rec[:] = b' ' * size
    for i, ch in enumerate(encoded[:size]):
        rec[i] = ch
    if len(encoded) < size:
        rec[len(encoded)] = eol
    return bytes(rec)

def send_printer_write(port: serial.Serial, text: str) -> None:
    """Send one SIO WRITE command frame + data record for the 825 printer ($40)."""
    # Assert CMD line (RTS active-low)
    port.rts = False      # RTS low = CMD asserted
    time.sleep(0.001)

    # Command frame: device $40, command $57 (WRITE), aux1=0, aux2=0
    frame = bytearray([0x40, 0x57, 0x00, 0x00])
    frame.append(sio_checksum(frame))
    port.write(bytes(frame))

    # Deassert CMD
    port.rts = True
    time.sleep(0.001)

    # Wait for ACK ('A')
    ack = port.read(1)
    if ack != b'A':
        raise RuntimeError(f"Expected ACK for command frame, got {ack!r}")
    print(f"  CMD ACK received")

    # Send 40-byte record + checksum
    record = make_record(text)
    cs = sio_checksum(record)
    port.write(record + bytes([cs]))

    # Expect ACK + COMPLETE ('A' then 'C')
    resp = port.read(2)
    if len(resp) < 2:
        raise RuntimeError(f"Incomplete response: {resp!r}")
    print(f"  DATA response: {resp!r}  (expect b'AC')")
    if resp[0:1] != b'A':
        raise RuntimeError("Expected ACK for data frame")
    if resp[1:2] != b'C':
        raise RuntimeError("Expected COMPLETE, got " + repr(resp[1:2]))
    print(f"  OK: '{text}'")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} /dev/ttyUSB0 \"text to print\"")
        sys.exit(1)

    dev  = sys.argv[1]
    text = sys.argv[2]

    with serial.Serial(dev, 19200, bytesize=8, parity='N', stopbits=1, timeout=1) as port:
        port.rts = True   # CMD deasserted initially
        time.sleep(0.1)
        send_printer_write(port, text)
```

Save as `inject_sio.py` and make it executable:

```bash
chmod +x inject_sio.py
```

---

## 5. Test Cases

For Atari computer tests, replace the `inject_sio.py` commands with the corresponding Atari BASIC `LPRINT` statement as shown.

---

### Test 5.1 — 825 personality: single LPRINT prints on ESC/P printer

**Compile with:** `ATARI_PRINTER_PERSONALITY=825`

**Option A (Atari BASIC):**
```basic
OPEN #1,8,0,"P:"
PRINT #1;"HELLO WORLD"
CLOSE #1
```
or equivalently:
```basic
LPRINT "HELLO WORLD"
```

**Option B (injector):**
```bash
python3 inject_sio.py /dev/ttyUSB0 "HELLO WORLD"
```

**Expected:**
- SIO response: `b'AC'` (ACK for data + COMPLETE)
- Printer prints "HELLO WORLD" on one line
- Paper advances (or receipt advances if thermal printer)

---

### Test 5.2 — 825 personality: multiple lines

**Option B:**
```bash
python3 inject_sio.py /dev/ttyUSB0 "Line One"
python3 inject_sio.py /dev/ttyUSB0 "Line Two"
python3 inject_sio.py /dev/ttyUSB0 "Line Three"
```

**Option A (BASIC):**
```basic
LPRINT "Line One"
LPRINT "Line Two"
LPRINT "Line Three"
```

**Expected:** Three lines printed in sequence on the same page/roll. No extra blank lines between them.

---

### Test 5.3 — 825 personality: ATASCII inverse-video character

ATASCII bytes $80–$BF are the inverse-video range. The `AtasciiConverter` maps them to the base character (stripping bit 7) or space for control codes.

**Option B — inject a record with `$C1` (inverse 'A'):**

```python
# inject_inverse.py
import serial, time, sys
# reuse sio_checksum and send helpers from inject_sio.py

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'

with serial.Serial(port, 19200, bytesize=8, parity='N', stopbits=1, timeout=1) as s:
    s.rts = True
    time.sleep(0.1)

    from inject_sio import sio_checksum, send_printer_write

    # Build record manually: 'T','E','S','T',$C1 (inverse 'A'),$9B, then spaces
    rec = bytearray(40)
    rec[:] = b' ' * 40
    rec[0] = ord('T'); rec[1] = ord('E'); rec[2] = ord('S'); rec[3] = ord('T')
    rec[4] = 0xC1   # inverse 'A' — should map to 'A'
    rec[5] = 0x9B   # EOL

    # Send as printer write
    s.rts = False
    time.sleep(0.001)
    frame = bytearray([0x40, 0x57, 0x00, 0x00])
    frame.append(sio_checksum(frame))
    s.write(bytes(frame))
    s.rts = True
    time.sleep(0.001)
    ack = s.read(1)
    assert ack == b'A', f"CMD ACK failed: {ack!r}"
    cs = sio_checksum(bytes(rec))
    s.write(bytes(rec) + bytes([cs]))
    resp = s.read(2)
    print(f"Response: {resp!r}")   # expect b'AC'
```

**Expected:** Printer prints "TESTA" — the inverse 'A' ($C1) is converted to plain 'A'. No garbled characters, no `?` substitution (since $C1 strips to $41 which is 'A', a printable ASCII).

---

### Test 5.4 — 1025 personality: 80-column join (two records → one line)

**Compile with:** `ATARI_PRINTER_PERSONALITY=1025`

The 1025 printer operates in COL_80 mode: two consecutive 40-byte SIO records are joined into a single 80-character line before being sent to the PCL generator.

**Option B:**
```bash
# Send two consecutive records — they should merge into one line
python3 inject_sio.py /dev/ttyUSB0 "$(python3 -c "print('A'*40, end='')")"
python3 inject_sio.py /dev/ttyUSB0 "B OK"
```

**Expected:**
- First record (40 × 'A', no EOL at byte 40) is buffered — **no output yet**
- Second record ("B OK" + EOL + spaces) is appended — joined line emitted
- Printed line: `AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB OK` (40 A's + "B OK")

> The first `inject_sio.py` call sends 40 bytes of 'A' with no `$9B`. The script must send the record **without** appending `$9B` for the join to work. Modify `make_record` to skip EOL when the text is exactly 40 characters:
>
> ```python
> # In inject_sio.py: if text fills all 40 bytes, no EOL is appended (addEol = False implicitly)
> rec = text.encode('ascii')[:40].ljust(40, b' ')
> ```

---

### Test 5.5 — TSPL personality: idle-timeout flush (label prints after 2 s idle)

**Compile with:** `ATARI_PRINTER_PERSONALITY=TSPL`

The TSPL personality uses an idle-timeout: if no new SIO record arrives within `TSPL_FLUSH_TIMEOUT_MS` (2000 ms), the accumulated label data is flushed and printed.

**Option B:**
```bash
python3 inject_sio.py /dev/ttyUSB0 "Label Line 1"
python3 inject_sio.py /dev/ttyUSB0 "Label Line 2"
python3 inject_sio.py /dev/ttyUSB0 "Label Line 3"
# Now wait > 2 seconds — do NOT send more records
sleep 3
```

**Expected:**
- During the 3-second sleep, the RP2040 `sioEmulator.tick()` detects the idle timeout
- A TSPL job is submitted to the printer manager
- Label prints with all three lines: "Label Line 1" / "Label Line 2" / "Label Line 3"
- Debug serial shows: `[INFO] SioPrinterEmulator: idle timeout flush triggered`

**Option A (BASIC):**
```basic
LPRINT "Label Line 1"
LPRINT "Label Line 2"
LPRINT "Label Line 3"
REM wait 3 seconds (END or idle)
```

---

### Test 5.6 — Bad checksum → NAK response

Inject a corrupted command frame checksum to verify NAK handling.

```python
# inject_bad_checksum.py — sends a command frame with an intentionally wrong checksum
import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'

with serial.Serial(port, 19200, bytesize=8, parity='N', stopbits=1, timeout=1) as s:
    s.rts = True
    time.sleep(0.1)

    # Assert CMD
    s.rts = False
    time.sleep(0.001)

    # Command frame with wrong checksum (last byte is intentionally 0xFF instead of correct value)
    frame = bytes([0x40, 0x57, 0x00, 0x00, 0xFF])
    s.write(frame)
    s.rts = True
    time.sleep(0.001)

    resp = s.read(1)
    print(f"Response: {resp!r}")   # expect b'N' (NAK)
    if resp == b'N':
        print("PASS: NAK received for bad checksum")
    else:
        print(f"FAIL: expected NAK, got {resp!r}")
```

```bash
python3 inject_bad_checksum.py /dev/ttyUSB0
```

**Expected:**
```
Response: b'N'
PASS: NAK received for bad checksum
```

The emulator returns NAK and resets to IDLE state (ready for the next frame).

---

### Test 5.7 — Flash size verification

The firmware must fit in the RP2040's 2 MB flash:

```bash
arm-none-eabi-size build_rp2040/platform/rp2040/atariusbprinter.elf \
  | awk 'NR==2 { total=$1+$2; printf "Flash used: %d bytes (%.1f KB) — limit 2097152 bytes\n", total, total/1024; if(total < 2097152) print "PASS"; else print "FAIL: exceeds 2 MB flash" }'
```

**Expected:**
```
Flash used: NNNNNN bytes (NNN.N KB) — limit 2097152 bytes
PASS
```

Phase 5 adds ≤ 32 KB over the Phase 2 RP2040 baseline.

---

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| No debug output on serial terminal | Wrong tty device or baud rate | `ls /dev/ttyACM*` after plugging; use 115200 baud |
| `Expected ACK, got b''` in injector | RTS line not wired correctly to GPIO26 | Verify wiring; check adapter supports hardware RTS; try `port.setRTS(False)` manually |
| `Expected ACK, got b'N'` (NAK) | CMD line timing too fast — RP2040 missed the CMD assertion | Increase `time.sleep(0.001)` after `port.rts = False` to 5 ms |
| Printer not enumerated after boot | USB host not recognising the printer | Check USB cable; ensure printer is powered; try different USB port on RP2040 board |
| 80-column join not working (two records → two short lines) | First record has `$9B` EOL byte at position < 40 | Ensure text exactly fills 40 bytes so no `$9B` is appended |
| TSPL idle timeout not triggering | `tick()` not called frequently enough or `TSPL_FLUSH_TIMEOUT_MS` value differs | Check `main_rp2040.cpp` main loop calls `sioEmulator.tick()` every iteration |
| Flash exceeds 2 MB | Too many features / optimisation level | Build with `Release` config: `-DCMAKE_BUILD_TYPE=Release` |
| Atari BASIC `LPRINT` gives `ERROR 138` | Printer device not responding on SIO | Check SIO cable pin wiring; verify 19200 baud setting; check CMD line polarity |

---

## 7. Phase 5 Acceptance Checklist

Mark each item after verifying:

- [ ] **5.1** 825/1027: `LPRINT "HELLO WORLD"` → ESC/P line prints on USB printer
- [ ] **5.2** 825/1027: Three consecutive LPRINTs → three lines printed in sequence
- [ ] **5.3** 825/1027: ATASCII inverse-video character ($C1) → correct ASCII letter ('A'), not garbled
- [ ] **5.4** 1025: Two consecutive 40-byte records join into one 80-char line on PCL printer
- [ ] **5.5** TSPL: Three LPRINTs + 2 s idle → label with all three lines prints
- [ ] **5.6** Bad checksum frame → NAK response byte; emulator returns to IDLE
- [ ] **5.7** Flash size: `arm-none-eabi-size` confirms < 2 097 152 bytes
- [ ] Unit tests pass: `ctest --test-dir build --output-on-failure` (especially `test_atascii_converter`, `test_line_assembler`, `test_sio_printer_emulator`)

All items checked = Phase 5 complete.
