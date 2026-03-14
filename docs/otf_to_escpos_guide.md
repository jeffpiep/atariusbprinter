# otf_to_escpos.py — User's Guide

Converts an OTF or TTF font file into ESC/POS user-defined character bitmap data
for 80 mm thermal (receipt) printers, targeting the ESC/POS Font A cell: **12 dots
wide × 24 dots tall**.

Outputs a C++ header for firmware embedding and/or a raw binary print-job file for
direct Linux testing.

---

## Prerequisites

```bash
pip install Pillow
```

Python 3.8+ required.  No other dependencies.

---

## ESC/POS Font A Cell Format

ESC/POS user-defined characters use the **ESC & (0x1B 0x26)** command to download
bitmaps into the printer's RAM.  The tool targets Font A — the printer's built-in
12 × 24 character cell:

```
Column 0        Column 1        ...     Column 11
┌──────────┐    ┌──────────┐            ┌──────────┐
│ byte 0   │    │ byte 0   │            │ byte 0   │  ← dots 0–7  (MSB = dot 0)
│ byte 1   │    │ byte 1   │            │ byte 1   │  ← dots 8–15
│ byte 2   │    │ byte 2   │            │ byte 2   │  ← dots 16–23
└──────────┘    └──────────┘            └──────────┘
```

Each column is 3 bytes (24 dots).  Each character requires `1 + 12×3 = 37 bytes`
(1 width byte + 36 column bytes).  The full printable ASCII range (0x20–0x7E,
95 chars) is `5 + 95×37 = 3520 bytes` to download.

The ESC & sequence header is: `1B 26 03 c1 c2` where `y=3` (bytes/column),
`c1`=first char code, `c2`=last char code.  Followed by `x, col0b0, col0b1, col0b2,
col1b0, ...` for each character.

After download, **ESC % 1** (0x1B 0x25 0x01) switches the printer to the
user-defined charset.  **ESC % 0** restores the built-in charset.

---

## Basic Usage

```bash
# Minimal: generate C++ header only (auto-fit point size)
python tools/otf_to_escpos.py MyFont.otf \
    -o include/generator/EscposFontData.h

# Generate a raw binary for immediate printer testing (no rebuild needed)
python tools/otf_to_escpos.py MyFont.otf \
    --test-bin /tmp/fonttest.bin

# Both outputs in one pass
python tools/otf_to_escpos.py MyFont.otf \
    -o include/generator/EscposFontData.h \
    --test-bin /tmp/fonttest.bin
```

---

## Options Reference

| Option | Default | Description |
|---|---|---|
| `font` | (required) | OTF or TTF font file path |
| `-o FILE.h` | — | Write C++ header with `kEscposFontDownload[]` |
| `--test-bin FILE.bin` | — | Write complete ESC/POS print job binary |
| `--point-size N` | auto | Set both X and Y point size to N |
| `--point-size-y N` | auto | Vertical (height) point size — controls how tall glyphs render in the 24-dot cell |
| `--point-size-x N` | =pt_y | Horizontal (width) point size — controls canvas width before crop to 12 dots |
| `--threshold N` | 128 | Brightness cutoff 0–255; pixels above this value become ink dots |
| `--first 0xNN` | 0x20 | First character code to render (hex or decimal) |
| `--last 0xNN` | 0x7E | Last character code to render |

At least one of `-o` or `--test-bin` must be specified.

---

## Point Size Tuning

The 12 × 24 cell is tiny.  Most fonts need tuning to fit well.

### Auto-fit (default)

When no point size is given, the tool scans from 24 downward and picks the largest
size where uppercase letters fit within 24 dots height.  This works well for fonts
with modest ascenders/descenders.

```bash
python tools/otf_to_escpos.py MyFont.otf --test-bin /tmp/test.bin
# Auto-fit point size (Y): 18   ← reported on stdout
```

### Independent X/Y scaling

`--point-size-x` and `--point-size-y` control the axes independently.  There is
**no resampling** — only crop — so proportions are always preserved:

- `--point-size-y N` — loads the font at size N.  When N > 24, the rendered glyph
  overflows the 24-dot cell; the tool pins the top to row 0 and clips the bottom
  (bottom-clip Y mode).  This lets you use larger sizes for fonts where ascenders
  dominate and you want to preserve the top detail.

- `--point-size-x N` — loads the font at size N to measure natural glyph widths, then
  uses that canvas width when rendering each character at `pt_y`.  If the canvas is
  wider than 12 px, the tool center-crops to 12.  If narrower, the glyph is centered
  with padding.  This lets you widen or narrow characters without distortion.

**Finding the right settings** — iterate with `--test-bin` and pipe the binary to the
printer (see [Sending Test Binaries](#sending-test-binaries-to-the-pos80)):

```bash
# Try progressively larger sizes and compare output on the printer
python tools/otf_to_escpos.py MyFont.otf \
    --point-size-x 18 --point-size-y 20 \
    --test-bin /tmp/t.bin && \
./build/tspl_print --escpos --vid 0483 --pid 5720 /tmp/t.bin
```

**Production settings for Atari-822-Thermal.otf:**

```bash
python tools/otf_to_escpos.py Atari-822-Thermal.otf \
    --point-size-x 20 --point-size-y 22 \
    --threshold 50 \
    -o include/generator/EscposFontData.h
```

---

## Threshold Tuning

The threshold controls how aggressively anti-aliased pixels are converted to ink.

- **Low threshold (e.g. 30–60)**: faint anti-aliasing pixels become ink → bolder, wider strokes.
  Good for fonts with thin strokes or when printing at lower contrast.
- **High threshold (e.g. 180–220)**: only bright pixels become ink → thinner, sharper glyphs.
  Good for fonts with thick strokes where you want detail.

Default is 128 (mid-point).  The Atari-822-Thermal.otf uses `--threshold 50` because
the font has delicate thin strokes that need the extra weight at 12 px width.

---

## Character Range

By default, the full printable ASCII range 0x20 (`space`) through 0x7E (`~`) is
rendered — 95 glyphs.

```bash
# Uppercase only (saves RAM and download time)
python tools/otf_to_escpos.py MyFont.otf \
    --first 0x41 --last 0x5A \
    --test-bin /tmp/upper.bin

# Digits only
python tools/otf_to_escpos.py MyFont.otf \
    --first 0x30 --last 0x39 \
    --test-bin /tmp/digits.bin
```

Note: ESC/POS user-defined characters can only replace chars in the 0x20–0x7E range.

---

## Outputs

### C++ Header (`-o FILE.h`)

The generated header defines a flat `constexpr uint8_t` array ready for inclusion in
firmware:

```cpp
// include/generator/EscposFontData.h  (gitignored — regenerate with otf_to_escpos.py)
static constexpr uint8_t kEscposFontDownload[3520] = {
    // ESC & y=3 c1=0x20 c2=0x7E
    0x1B, 0x26, 0x03, 0x20, 0x7E,
    // ' ' (0x20)
    0x0C, 0x00, 0x00, 0x00, ...
    // '!' (0x21)
    0x0C, 0x00, 0x00, 0x00, ...
    ...
};
static constexpr size_t kEscposFontDownloadSize = 3520;
```

The header is **gitignored** — regenerate it whenever you change font settings, then
rebuild the firmware.  After regenerating, rebuild and the new font is live:

```bash
python tools/otf_to_escpos.py Atari-822-Thermal.otf \
    --point-size-x 20 --point-size-y 22 --threshold 50 \
    -o include/generator/EscposFontData.h
cmake --build build -j$(nproc)
```

### Test Binary (`--test-bin FILE.bin`)

Produces a complete, self-contained ESC/POS print job:

1. `ESC @` — printer init / reset
2. ESC & download sequence (full charset)
3. `ESC % 1` — activate user-defined charset
4. Three text lines:
   - `ABCDEFGHIJKLMNOPQRSTUVWXYZ`
   - `abcdefghijklmnopqrstuvwxyz`
   - `0123456789 !"#$%&'()*+,-./:;<=>?@[\]^_{|}~`
5. `ESC % 0` — restore built-in charset
6. `ESC d 4` — feed 4 lines
7. `GS V 66 0` — partial cut

Send it directly to the printer with the `tspl_print` CLI (no rebuild needed):

```bash
./build/tspl_print --escpos --vid 0483 --pid 5720 /tmp/fonttest.bin
```

---

## Sending Test Binaries to the POS80

The `tspl_print` CLI's `--escpos` mode sends any raw binary file directly to the
printer over USB, bypassing all protocol detection:

```bash
# Explicit VID/PID (POS80 = 0483:5720)
./build/tspl_print --escpos --vid 0483 --pid 5720 /tmp/fonttest.bin

# Auto-detect first USB printer-class device
./build/tspl_print --escpos /tmp/fonttest.bin
```

Prerequisites:
- Printer connected and powered on
- udev rule grants access: `/etc/udev/rules.d/99-tspl-printer.rules`
  ```
  SUBSYSTEM=="usb", ENV{ID_USB_INTERFACES}=="*:0701??:*", MODE="0666"
  ```
- Binary built: `cmake -B build && cmake --build build -j$(nproc)`

---

## Integration with EscposTextGenerator (Firmware)

The generated `kEscposFontDownload[]` array integrates with `EscposTextGenerator`:

```cpp
#include "generator/EscposFontData.h"

EscposTextGenerator gen;
gen.setCustomFont(kEscposFontDownload, kEscposFontDownloadSize);
// Font is downloaded automatically on first writeLine()

gen.writeLine("Hello, World!");
auto job = gen.flush();
```

On first `writeLine()`, the generator sends ESC @, the full ESC & download sequence,
and ESC % 1 before the text.  Subsequent lines skip the download (tracked internally).
Call `markFontDownloaded()` to suppress re-download if you pre-sent the font at
attach time.

---

## Full Workflow Example

```bash
# 1. Generate font header + test binary
python tools/otf_to_escpos.py Atari-822-Thermal.otf \
    --point-size-x 20 --point-size-y 22 \
    --threshold 50 \
    -o include/generator/EscposFontData.h \
    --test-bin /tmp/fonttest.bin

# 2. Quick visual check — send test binary to printer
./build/tspl_print --escpos --vid 0483 --pid 5720 /tmp/fonttest.bin

# 3. Rebuild firmware with new font header
cmake --build build -j$(nproc)

# 4. Full firmware font test via CLI
./build/tspl_print --escpos-fontdl-all --vid 0483 --pid 5720
```

The `--escpos-fontdl-all` command exercises the full firmware path: it instantiates
`EscposTextGenerator`, loads the embedded font, and prints the same three test lines.

---

## Demonstration Script

See [tools/escpos_font_demo.py](../tools/escpos_font_demo.py) for a standalone script
that:

1. Renders the font using the production settings
2. Builds a custom print job with the full character set and the quick-brown-fox
   sentence
3. Sends the job directly to the POS80 printer

```bash
# Run with defaults (Atari-822-Thermal.otf, POS80 0483:5720)
python tools/escpos_font_demo.py

# Use a different font
python tools/escpos_font_demo.py --font /path/to/Other.otf

# Override printer VID/PID
python tools/escpos_font_demo.py --vid 04b8 --pid 1187
```
