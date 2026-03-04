# Phase 4 Hardware Testing Guide

**Binary:** `build/tspl_print`
**Focus:** Text generator layer — `TsplTextGenerator`, `PclTextGenerator`, `EscpTextGenerator`
**Requirement:** Hardware from at least one printer category (TSPL, PCL, or ESC/P). Tests are labelled by printer type; complete what you have.

> **There is no `--text-mode` flag in `tspl_print`.** Phase 4 validates the text generator layer by using Python scripts that produce byte-for-byte identical output to what each generator emits. These scripts are verified against the unit tests. Send the generated files to the appropriate printer exactly as you would any raw job file.

---

## 1. Prerequisites

### 1.1 Hardware

- **TSPL test:** Any TSPL/ZPL label printer (e.g. Zebra ZD421, TSC TTP-244, AiYin)
- **PCL test:** Any PCL5/PCL5e printer (HP LaserJet, Brother laser, HP OfficeJet)
- **ESC/P test:** Any ESC/P or ESC/POS printer (Epson dot-matrix, Epson TM-T20, Star TSP100)

At minimum one printer is needed to complete at least one test track.

### 1.2 Software

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

All of `test_tspl_text_generator`, `test_pcl_text_generator`, and `test_escp_text_generator` must pass before hardware testing.

---

## 2. USB Access (udev Rule)

See **Phase 1 §2** for the full udev setup procedure.

---

## 3. Verify Detection

```bash
./build/tspl_print --list
```

Confirm your target printer(s) appear in the list.

---

## 4. Test Assets

The following Python scripts reproduce the exact byte output of each text generator. Each script writes to stdout in binary mode. Run each once to create the input files.

### 4.1 TSPL 3-line label (`gen_tspl_label.py`)

Mimics `TsplTextGenerator` with default config (50 mm × 30 mm label, 8 dots/mm, 24-dot font, y starts at 10, line height 24 dots):

```python
# Save as gen_tspl_label.py
import sys

lines = ["Phase 4 Test", "Text Generator", "TSPL OK"]
out   = b'SIZE 50 mm,30 mm\r\nGAP 3 mm,0 mm\r\nDIRECTION 0\r\nCLS\r\n'
y = 10
for line in lines:
    out += f'TEXT 10,{y},"TSS24.BF2",0,1,1,"{line}"\r\n'.encode()
    y += 24
out += b'PRINT 1,1\r\n'

sys.stdout.buffer.write(out)
```

```bash
python3 gen_tspl_label.py > gen_tspl_label.tspl
```

### 4.2 TSPL 10-line label — tests auto page-break (`gen_tspl_10lines.py`)

With default line height of 24 dots and a 30 mm (240-dot) label, 10 lines at 24 dots each = 240 dots. The generator auto-flushes when `currentY > labelHeightDots`, producing a second label. Adjust the SIZE to accommodate how your printer is configured.

```python
# Save as gen_tspl_10lines.py
import sys

# Using a tall label to force two pages: 30 mm = 240 dots; 10×24 = 240 (triggers wrap)
# Switch to a taller label (60 mm) to see the split more clearly
label_h_mm = 60   # set to match your loaded label height
label_h_dots = label_h_mm * 8

lines = [f"Line {i+1}" for i in range(10)]

out = b''
y = 10
label_start = b''
for i, line in enumerate(lines):
    if y == 10:
        label_start = f'SIZE 50 mm,{label_h_mm} mm\r\nGAP 3 mm,0 mm\r\nDIRECTION 0\r\nCLS\r\n'.encode()
        out += label_start
    out += f'TEXT 10,{y},"TSS24.BF2",0,1,1,"{line}"\r\n'.encode()
    y += 24
    if y + 24 > label_h_dots and i < len(lines) - 1:
        out += b'PRINT 1,1\r\n'
        y = 10

out += b'PRINT 1,1\r\n'
sys.stdout.buffer.write(out)
```

```bash
python3 gen_tspl_10lines.py > gen_tspl_10lines.tspl
```

### 4.3 PCL 5-line page (`gen_pcl_text.py`)

Mimics `PclTextGenerator` default config (10 CPI, 6 LPI, Courier):

```python
# Save as gen_pcl_text.py
import sys

lines  = [f"PCL Line {i+1}" for i in range(5)]
header = b'\x1b\x45\x1b&l0O\x1b&l6D\x1b(10U\x1b(s0p10h12v0s0b3T'
body   = b''.join(l.encode() + b'\r\n' for l in lines)
footer = b'\x0c\x1b\x45'

sys.stdout.buffer.write(header + body + footer)
```

```bash
python3 gen_pcl_text.py > gen_pcl_text.bin
```

### 4.4 PCL 70-line job — tests form-feed at line 60 (`gen_pcl_70lines.py`)

Mimics `PclTextGenerator` with `pclLinesPerPage = 60`. At line 60 a `\x0c` is inserted; the remaining 10 lines go on the second page, followed by a final `\x0c`:

```python
# Save as gen_pcl_70lines.py
import sys

header = b'\x1b\x45\x1b&l0O\x1b&l6D\x1b(10U\x1b(s0p10h12v0s0b3T'
body   = b''
for i in range(70):
    body += f'PCL line {i+1:02d}\r\n'.encode()
    if (i + 1) % 60 == 0:
        body += b'\x0c'     # page break at line 60
footer = b'\x0c\x1b\x45'   # final form feed + reset

sys.stdout.buffer.write(header + body + footer)
```

```bash
python3 gen_pcl_70lines.py > gen_pcl_70lines.bin
```

### 4.5 ESC/P 5-line page (`gen_escp_text.py`)

Mimics `EscpTextGenerator` default config (NLQ, 10 CPI, 6 LPI):

```python
# Save as gen_escp_text.py
import sys

lines = [f"ESC/P Line {i+1}" for i in range(5)]
init  = b'\x1b@\x1bx\x01\x1bP\x1b3\x24'
body  = b''.join(l.encode() + b'\r\n' for l in lines)
ff    = b'\x0c'

sys.stdout.buffer.write(init + body + ff)
```

```bash
python3 gen_escp_text.py > gen_escp_text.bin
```

### 4.6 ESC/P no form feed — receipt printers (`gen_escp_noff.py`)

Mimics `EscpTextGenerator` with `suppressFormFeed = true`:

```python
# Save as gen_escp_noff.py
import sys

lines = ["ESC/P No Form Feed", "Receipt mode", "Suppress FF OK"]
init  = b'\x1b@\x1bx\x01\x1bP\x1b3\x24'
body  = b''.join(l.encode() + b'\r\n' for l in lines)

sys.stdout.buffer.write(init + body)   # no \x0c at end
```

```bash
python3 gen_escp_noff.py > gen_escp_noff.bin
```

### 4.7 ESC/P condensed mode (`gen_escp_condensed.py`)

Mimics `EscpTextGenerator` with `escpCondensed = true` (17 CPI via `SI` = `0x0F`):

```python
# Save as gen_escp_condensed.py
import sys

lines = ["Condensed 17 CPI", "More chars per line", "ESC/P Condensed OK"]
init  = b'\x1b@\x0f\x1b3\x24'          # ESC @ then SI (condensed), then line spacing
body  = b''.join(l.encode() + b'\r\n' for l in lines)
ff    = b'\x0c'

sys.stdout.buffer.write(init + body + ff)
```

```bash
python3 gen_escp_condensed.py > gen_escp_condensed.bin
```

---

## 5. Test Cases

---

### Test 5.1 — TSPL 3-line label from generator output

**Command:**
```bash
./build/tspl_print gen_tspl_label.tspl
```

**Expected:**
- Label prints with three lines: "Phase 4 Test" / "Text Generator" / "TSPL OK"
- Text is evenly spaced (24 dots / ~3 mm between lines)

---

### Test 5.2 — TSPL auto page-break (10 lines → 2 labels)

```bash
./build/tspl_print gen_tspl_10lines.tspl
```

**Expected:**
- Two labels are printed
- First label contains lines 1–N (those that fit within `label_h_dots`)
- Second label contains the remaining lines
- No lines are dropped

Verify by counting the labels physically. If only one label prints, increase `label_h_mm` in `gen_tspl_10lines.py` — the auto-break threshold depends on the loaded media size matching the `SIZE` parameter.

---

### Test 5.3 — PCL 5-line page from generator output

```bash
./build/tspl_print gen_pcl_text.bin
```

**Expected:**
- Printer ejects one page
- Page contains "PCL Line 1" through "PCL Line 5" in Courier font

---

### Test 5.4 — PCL 70-line job produces 2 pages

```bash
./build/tspl_print gen_pcl_70lines.bin
```

**Expected:**
- Printer ejects two pages
- First page: lines 1–60
- Second page: lines 61–70

Verify by counting printed pages and checking the line numbers at the bottom of the first page and top of the second.

---

### Test 5.5 — ESC/P 5-line page from generator output

```bash
./build/tspl_print gen_escp_text.bin
```

**Expected:**
- Printer ejects (or advances paper)
- Page contains "ESC/P Line 1" through "ESC/P Line 5"

---

### Test 5.6 — ESC/P no form feed (receipt printer doesn't eject)

```bash
./build/tspl_print gen_escp_noff.bin
```

**Expected:**
- Printer prints "ESC/P No Form Feed" / "Receipt mode" / "Suppress FF OK"
- Paper does **not** auto-cut or eject after the job
- Printer waits for more data

This test confirms `suppressFormFeed` works end-to-end: the generator omits `\x0c`, the handler sends all bytes, the printer receives them without a page-advance command.

---

### Test 5.7 — ESC/P condensed mode (17 CPI)

```bash
./build/tspl_print gen_escp_condensed.bin
```

**Expected:**
- Text prints noticeably narrower than normal 10 CPI output
- "Condensed 17 CPI" fits on one line with room to spare
- Text is readable (not garbled)

---

## 6. Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| TSPL label prints blank | Label size in `SIZE` command doesn't match loaded media | Update `SIZE` to match actual label dimensions; run printer self-test to confirm media |
| TSPL text clipped at bottom | Y coordinate exceeds label height in dots | Reduce `label_h_dots` or the number of lines |
| PCL page has wrong margins | Printer's PCL default margins differ | Add `\x1b&a0L` (left margin 0) and `\x1b&a0M` (right margin max) to the header |
| ESC/P text prints in DPI or draft quality | Printer ignored `\x1bx\x01` NLQ command | Some dot-matrix printers need font selection too; this is a printer limitation |
| ESC/P no paper advance after `\x0c` | Printer is a receipt printer and `\x0c` is treated as data | Use `gen_escp_noff.py` as the receipt baseline; add `GS V` cut command if needed |
| `Protocol auto-detected: TSPL` for a PCL/ESC/P file | File starts with bytes that match TSPL probe | Confirm file was generated correctly; `xxd gen_pcl_text.bin | head` should start with `1b 45` |
| Condensed mode not working | Printer doesn't support ESC/P `SI` | Check printer manual; some receipt printers use a different condensed command |

---

## 7. Phase 4 Acceptance Checklist

Mark each item after verifying:

- [ ] **5.1** TSPL 3-line label prints correctly (text matches, spacing correct)
- [ ] **5.2** TSPL 10-line job produces 2 labels (auto page-break fires at line height limit)
- [ ] **5.3** PCL 5-line page prints in Courier font
- [ ] **5.4** PCL 70-line job produces 2 pages (form feed at line 60)
- [ ] **5.5** ESC/P 5-line page prints and paper advances
- [ ] **5.6** ESC/P `suppressFormFeed` file does not eject paper
- [ ] **5.7** ESC/P condensed mode produces narrower 17 CPI text
- [ ] Unit tests pass: `ctest --test-dir build --output-on-failure` (especially `test_tspl_text_generator`, `test_pcl_text_generator`, `test_escp_text_generator`)

All items checked = Phase 4 complete.
