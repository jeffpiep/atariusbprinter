# Phase 3 Hardware Testing Guide

**Binary:** `build/tspl_print`
**Focus:** PCL protocol handler (laser/inkjet printers), ESC/P protocol handler (dot-matrix and receipt printers), IEEE 1284 device ID assisted detection
**Requirement:** At least one PCL printer **or** one ESC/P printer. Tests are labelled accordingly; skip any that require hardware you don't have.

---

## 1. Prerequisites

### 1.1 Hardware

**PCL printer** (any one of):
- HP LaserJet (any model that accepts PCL5 or PCL5e — virtually all HP lasers)
- Brother laser printer (HL or MFC series)
- HP OfficeJet or ENVY (PCL3/PCL5e inkjet)
- Canon PIXMA (PCL5e-capable models, e.g. iX6820)

**ESC/P printer** (any one of):
- Epson dot-matrix (LQ, FX, or DFX series)
- Epson thermal receipt: TM-T20, TM-T88 series
- Star Micronics receipt: TSP100, TSP650
- Bixolon thermal: SRP-330, SRP-350

Both printer categories can be tested simultaneously if available.

### 1.2 Software

```bash
# Build and run tests first
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Ensure `test_pcl_handler` and `test_escp_handler` both pass before hardware testing.

---

## 2. USB Access (udev Rule)

USB access requires a udev rule for non-root use. See **Phase 1 §2** for the full setup procedure.

The printer-class rule covers all brands:

```bash
sudo tee /etc/udev/rules.d/99-tspl-printer.rules <<'EOF'
SUBSYSTEM=="usb", ENV{ID_USB_INTERFACES}=="*:070100:*", MODE="0666"
EOF
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Re-plug all printers after applying.

---

## 3. Verify Detection

```bash
./build/tspl_print --list
```

Example with one PCL printer and one ESC/P receipt printer:

```
Found 2 USB printer(s):
  [0] 03f0:0c17 bus1 addr7 "HP LaserJet P1005"
  [1] 04b8:0202 bus1 addr8 "EPSON TM-T20"
```

---

## 4. Test Assets

The following Python one-liners generate raw binary job files used in the test cases. Run them once and keep the files for repeated use.

### 4.1 PCL job — single page (`gen_pcl.py`)

```python
# Save as gen_pcl.py
import sys

data  = b'\x1b\x45'                          # PCL reset
data += b'\x1b&l0O'                           # portrait orientation
data += b'\x1b&l6D'                           # 6 LPI
data += b'\x1b(10U'                           # PC-8 symbol set
data += b'\x1b(s0p10h12v0s0b3T'              # Courier 10 CPI
data += b'Phase 3 PCL Test\r\n'
data += b'USB Printer Driver\r\n'
data += b'PCL Handler OK\r\n'
data += b'\x0c'                               # form feed (eject page)
data += b'\x1b\x45'                           # PCL reset

sys.stdout.buffer.write(data)
```

```bash
python3 gen_pcl.py > test_pcl.bin
```

### 4.2 PCL job — multi-page, 70 lines (`gen_pcl_70.py`)

```python
# Save as gen_pcl_70.py
import sys

header = b'\x1b\x45\x1b&l0O\x1b&l6D\x1b(10U\x1b(s0p10h12v0s0b3T'
lines  = b''.join(f'PCL line {i+1:02d}\r\n'.encode() for i in range(70))
footer = b'\x0c\x1b\x45'

sys.stdout.buffer.write(header + lines + footer)
```

```bash
python3 gen_pcl_70.py > test_pcl_70.bin
```

### 4.3 ESC/P job — single page (`gen_escp.py`)

```python
# Save as gen_escp.py
import sys

data  = b'\x1b@'                              # ESC/P initialise
data += b'\x1bx\x01'                          # NLQ (letter quality) mode
data += b'\x1bP'                              # 10 CPI (pica)
data += b'\x1b3\x24'                          # line spacing: 216/6=36=0x24 (6 LPI)
data += b'Phase 3 ESC/P Test\r\n'
data += b'USB Printer Driver\r\n'
data += b'ESC/P Handler OK\r\n'
data += b'\x0c'                               # form feed

sys.stdout.buffer.write(data)
```

```bash
python3 gen_escp.py > test_escp.bin
```

### 4.4 ESC/POS receipt job with paper cut (`gen_escpos_cut.py`)

For thermal receipt printers that support ESC/POS paper cut:

```python
# Save as gen_escpos_cut.py
import sys

data  = b'\x1b@'                              # ESC/P init (compatible preamble)
data += b'Phase 3 Receipt Test\r\n'
data += b'ESC/POS Paper Cut\r\n'
data += b'\n\n\n'                             # advance paper
data += b'\x1d\x56\x42\x00'                  # GS V B 0 — full cut

sys.stdout.buffer.write(data)
```

```bash
python3 gen_escpos_cut.py > test_escpos_cut.bin
```

---

## 5. Test Cases

---

### Test 5.1 — PCL auto-detection on PCL printer

**Command:**
```bash
./build/tspl_print --verbose test_pcl.bin 2>&1 | grep -E "(auto-detect|sendJob|complete)"
```

**Expected:**
```
[INFO ][      0 ms][PrinterManager  ] Protocol auto-detected: PCL
[INFO ][      0 ms][PclHandler      ] sendJob 'test_pcl.bin': NNN bytes in chunks of 4096
[INFO ][    100 ms][PclHandler      ] sendJob 'test_pcl.bin': complete
```

The printer should eject a page containing "Phase 3 PCL Test", "USB Printer Driver", and "PCL Handler OK".

---

### Test 5.2 — PCL multi-page job (70 lines → 2 pages)

The PCL handler sends raw bytes; page breaks come from the `\x0c` embedded in the file. The 70-line file contains only a single form feed at the end (one page).

**Command:**
```bash
./build/tspl_print --verbose test_pcl_70.bin
```

**Expected:** One page is ejected containing 70 lines of "PCL line NN". (If the printer physically runs out of page space it may eject early — this is a printer firmware behaviour, not a driver bug.)

To generate a two-page PCL job explicitly with an embedded form feed:

```bash
python3 -c "
import sys
hdr = b'\x1b\x45\x1b&l0O\x1b&l6D\x1b(10U\x1b(s0p10h12v0s0b3T'
p1  = b''.join(f'Page 1 line {i+1}\r\n'.encode() for i in range(10))
p2  = b''.join(f'Page 2 line {i+1}\r\n'.encode() for i in range(10))
sys.stdout.buffer.write(hdr + p1 + b'\x0c' + p2 + b'\x0c\x1b\x45')
" > test_pcl_2page.bin

./build/tspl_print test_pcl_2page.bin
```

**Expected:** Printer ejects two pages.

---

### Test 5.3 — ESC/P auto-detection on ESC/P printer

**Command:**
```bash
./build/tspl_print --verbose test_escp.bin 2>&1 | grep -E "(auto-detect|sendJob|complete)"
```

**Expected:**
```
[INFO ][      0 ms][PrinterManager  ] Protocol auto-detected: ESCP
[INFO ][      0 ms][EscpHandler     ] sendJob 'test_escp.bin': NNN bytes in chunks of 4096
[INFO ][    100 ms][EscpHandler     ] sendJob 'test_escp.bin': complete
```

Printer ejects (or advances) with "Phase 3 ESC/P Test", "USB Printer Driver", "ESC/P Handler OK".

---

### Test 5.4 — ESC/POS receipt with paper cut (thermal receipt printer)

```bash
./build/tspl_print test_escpos_cut.bin
```

**Expected:**
- Receipt prints with "Phase 3 Receipt Test" and "ESC/POS Paper Cut"
- Paper auto-cuts after the `GS V` command
- No error output from the driver

> If the printer does not cut, it may not support `GS V B` full cut. Try `GS V A` (partial cut): replace `b'\x1d\x56\x42\x00'` with `b'\x1d\x56\x41'` in `gen_escpos_cut.py`.

---

### Test 5.5 — IEEE 1284 device ID in verbose output

Many modern USB printers expose an IEEE 1284 device ID string on the read endpoint. With `--verbose`, this should appear in the log before the probe loop runs.

```bash
./build/tspl_print --verbose test_pcl.bin 2>&1 | grep -i "1284"
```

**Expected (bidirectional printers only):**
```
[DEBUG][      0 ms][PrinterManager  ] IEEE 1284 ID: MFG:Hewlett-Packard;CMD:PCL;MDL:HP LaserJet P1005;...
[DEBUG][      0 ms][PrinterManager  ] IEEE 1284 hint: PCL
```

**If no 1284 ID appears:** The printer is unidirectional (write-only). The driver falls through to the `probe()` loop automatically — this is not an error.

---

### Test 5.6 — Protocol probe: TSPL job sent to a PCL printer

This verifies that the driver correctly dispatches TSPL bytes to the TSPL handler even when the attached printer is a PCL device. The printer will silently reject or print garbage (expected — the driver sends bytes correctly).

```bash
# With the PCL printer connected, send a TSPL file
./build/tspl_print --verbose test_label.tspl 2>&1 | grep "auto-detect"
```

**Expected:**
```
[INFO ][      0 ms][PrinterManager  ] Protocol auto-detected: TSPL
```

The driver correctly identifies the data as TSPL (regardless of the connected hardware). The PCL printer receives TSPL bytes and ignores or prints them as text — this is correct behaviour.

---

### Test 5.7 — Handler probe order: TSPL → ESCP → PCL

With a single printer connected, verify that probe order is respected by checking which handler wins for ambiguous data.

ESC/P data starts with `\x1b@`. PCL data starts with `\x1b\x45`. TSPL data starts with `SIZE`.

```bash
# ESC/P data → ESC/P wins (TSPL probe fails first)
./build/tspl_print --verbose test_escp.bin 2>&1 | grep "auto-detect"

# PCL data → PCL wins (TSPL and ESCP probes fail first)
./build/tspl_print --verbose test_pcl.bin 2>&1 | grep "auto-detect"
```

**Expected:**
```
Protocol auto-detected: ESCP
Protocol auto-detected: PCL
```

Probe order is TSPL → ESCP → PCL; the first match wins.

---

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `Protocol auto-detected: TSPL` for a PCL file | File starts with bytes matching TSPL probe (`SIZE`) | Verify file was generated from `gen_pcl.py`; `xxd test_pcl.bin | head` should show `1b 45` |
| `Protocol auto-detected: UNKNOWN` | No handler's `probe()` matches the first bytes | Confirm file starts with `\x1b\x45` (PCL) or `\x1b@` (ESC/P); use `xxd file | head` |
| Printer receives job but prints garbled text | Mismatch: PCL file sent to ESC/P printer or vice versa | Confirm correct printer is connected; use `--verbose` to check auto-detection result |
| ESC/P receipt printer doesn't cut paper | Printer doesn't support `GS V B` full cut | Try partial cut `GS V A`; check printer manual for supported cut commands |
| `No printer-class interface found` | Kernel `usblp` driver loaded | `sudo rmmod usblp` (see Phase 1 §6) |
| IEEE 1284 hint not shown even with `--verbose` | Printer is unidirectional (write-only) | Normal — driver falls back to probe loop automatically |
| `write failed: Pipe error` mid-job | USB cable too long or intermittent connection | Try a shorter cable or a powered USB hub |

---

## 7. Phase 3 Acceptance Checklist

Mark each item after verifying:

- [ ] **5.1** PCL page printed on PCL printer; log shows "Protocol auto-detected: PCL"
- [ ] **5.2** Multi-page PCL job (two `\x0c` embedded) produces two pages
- [ ] **5.3** ESC/P page printed on ESC/P printer; log shows "Protocol auto-detected: ESCP"
- [ ] **5.4** ESC/POS receipt prints and paper cuts (if receipt printer available)
- [ ] **5.5** IEEE 1284 hint appears in `--verbose` output (if bidirectional printer available)
- [ ] **5.6** TSPL data sent to PCL-attached printer is correctly identified as TSPL
- [ ] **5.7** Probe order confirmed: TSPL → ESCP → PCL for respective job files
- [ ] Unit tests pass: `ctest --test-dir build --output-on-failure` (especially `test_pcl_handler`, `test_escp_handler`)

All items checked = Phase 3 complete.
