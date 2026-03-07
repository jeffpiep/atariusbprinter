# ESC ~ Sequence Reference

The Atari USB Printer Driver supports a set of configuration sequences sent via `LPRINT`
from Atari BASIC (or any program that writes to the `P:` device). These sequences control
TSPL label dimensions, font settings, margins, flash storage, and print triggering.

> **Note:** Sequences are only active when the driver is configured for a **TSPL** printer
> personality. They are silently ignored for PCL and ESC/P personalities.

---

## Sequence Format

Every sequence is exactly **4 bytes**:

```
ESC  ~   C   V
0x1B 0x7E cmd val
```

| Byte | Value | Meaning |
|------|-------|---------|
| 1 | `0x1B` | ESC (Escape) |
| 2 | `0x7E` | `~` (tilde — namespace byte) |
| 3 | command | Single letter (see table below) |
| 4 | value | Numeric argument (0–255) |

In Atari BASIC, ESC is printable in listing and obtained by pressing ESC twice. The equivalent ATASCII code for '~' is DELETE. Press ESC BACKSP to get the printable character. The the sequences read naturally:

```basic
LPRINT "[ESC][~]W";CHR$(58)
```

---

## Command Reference

### Label Dimensions

| Sequence | Command | Value | Effect |
|----------|---------|-------|--------|
| `ESC ~ W n` | Width | 1–255 | Label width = n mm → n × 8 dots (203 dpi) |
| `ESC ~ H n` | Height | 1–255 | Label height = n mm → n × 8 dots |
| `ESC ~ G n` | Gap | 0–255 | Gap between labels = n × 0.1 mm (rounded to dots) |

**Rules:**
- `W` and `H`: value 0 is rejected (silently ignored).
- `G`: value 0 is valid — sets a gapless (continuous) feed.

### Font Settings

| Sequence | Command | Value | Effect |
|----------|---------|-------|--------|
| `ESC ~ F n` | Font ID | 1–8 | TSPL built-in font number |
| `ESC ~ X n` | X multiplier | 1–8 | Horizontal character scale factor |
| `ESC ~ Y n` | Y multiplier | 1–8 | Vertical character scale factor |

**Rules:**
- `F`, `X`, `Y`: values 0 and 9–255 are rejected.

### Margins

| Sequence | Command | Value | Effect |
|----------|---------|-------|--------|
| `ESC ~ L n` | Left margin | 0–255 | Left margin = n mm → n × 8 dots |
| `ESC ~ T n` | Top margin | 0–255 | Top margin = n mm → n × 8 dots |

**Rules:**
- Both accept value 0 (zero margin is valid).

### Config Reset

| Sequence | Command | Value | Effect |
|----------|---------|-------|--------|
| `ESC ~ D 0` | Defaults | must be 0 | Reset all settings to compiled-in defaults |

**Rules:**
- Value must be `0`; any other value is ignored.
- This does **not** reload from flash — it restores the firmware's compile-time defaults.

### Flash Storage (NVM)

| Sequence | Command | Value | Effect |
|----------|---------|-------|--------|
| `ESC ~ S n` | Save | 0–9 | Save current settings to flash slot n |
| `ESC ~ R n` | Read | 0–9 | Load settings from flash slot n |

**Rules:**
- 10 slots available (0–9). Values 10–255 are rejected.
- `ESC ~ S` persists: width, height, gap, font ID, X/Y multipliers, margins, and
  all PCL/ESC/P fields. The `jobId` string is **not** stored.
- `ESC ~ R` loads settings immediately; the next printed line uses them.
- **Boot behavior:** Slot 0 is automatically loaded on power-up. If slot 0 is empty
  or corrupt, compiled-in defaults are used.
- Flash write (`ESC ~ S`) takes ~20 ms and briefly suspends USB and SIO interrupts.
  Send it between labels, not mid-print.

### Print Trigger

| Sequence | Command | Value | Effect |
|----------|---------|-------|--------|
| `ESC ~ P n` | Print | ignored | Flush accumulated lines and send label to printer |

**Rules:**
- Value byte is ignored (any value works, convention is `0`).
- Equivalent to pressing the GPIO 8 button.
- No SIO-quiet guard needed — the trigger is processed after the SIO transaction completes.

---

## Timing and Persistence

- Settings take effect on the **next** `LPRINT` line that produces text output.
- A record containing only ESC ~ sequences (no text) produces **no output line** — it
  simply updates the pending configuration.
- Settings **persist across labels** until changed or reset with `ESC ~ D 0`.
- Settings survive power cycles when saved to flash with `ESC ~ S n`.

---

## Atari BASIC Examples

### Set label size and font, then print

```basic
10 REM Configure 58mm × 30mm label, double-wide font
20 LPRINT CHR$(27);"~W";CHR$(58)   : REM Width 58 mm
30 LPRINT CHR$(27);"~H";CHR$(30)   : REM Height 30 mm
40 LPRINT CHR$(27);"~G";CHR$(30)   : REM Gap 3 mm (30 × 0.1)
50 LPRINT CHR$(27);"~X";CHR$(2)    : REM Double-wide text
60 LPRINT "Hello, World!"
70 LPRINT CHR$(27);"~P";CHR$(0)    : REM Print now
```

### Save configuration to flash slot 0

```basic
10 REM After setting up, save to slot 0 so it survives resets
20 LPRINT CHR$(27);"~W";CHR$(58)
30 LPRINT CHR$(27);"~H";CHR$(40)
40 LPRINT CHR$(27);"~S";CHR$(0)    : REM Save to slot 0
```

### Use multiple slots for different printers

```basic
10 REM Slot 0 = 58mm label printer
20 LPRINT CHR$(27);"~W";CHR$(58)
30 LPRINT CHR$(27);"~H";CHR$(30)
40 LPRINT CHR$(27);"~S";CHR$(0)

50 REM Slot 1 = 100mm wide shipping label printer
60 LPRINT CHR$(27);"~W";CHR$(100)
70 LPRINT CHR$(27);"~H";CHR$(150)
80 LPRINT CHR$(27);"~S";CHR$(1)

90 REM Switch to slot 1 at runtime:
100 LPRINT CHR$(27);"~R";CHR$(1)
```

### Batch print loop with programmatic print trigger

```basic
10 FOR I=1 TO 5
20   LPRINT "Item number ";I
30   LPRINT CHR$(27);"~P";CHR$(0)  : REM Print each label immediately
40 NEXT I
```

### Reset to firmware defaults

```basic
10 LPRINT CHR$(27);"~D";CHR$(0)    : REM Restore compiled-in defaults
```

---

## Quick Reference Card

```
ESC ~ W n   Width       n mm  (1–255, val=0 ignored)
ESC ~ H n   Height      n mm  (1–255, val=0 ignored)
ESC ~ G n   Gap         n×0.1 mm (0=gapless)
ESC ~ F n   Font ID     1–8
ESC ~ X n   X scale     1–8
ESC ~ Y n   Y scale     1–8
ESC ~ L n   Left margin n mm  (0=flush left)
ESC ~ T n   Top margin  n mm  (0=flush top)
ESC ~ D 0   Defaults    reset to firmware defaults (val must be 0)
ESC ~ S n   Save        save to flash slot n (0–9, survives power cycle)
ESC ~ R n   Read        load from flash slot n (0–9)
ESC ~ P *   Print       flush & print label now (val ignored)
```

_1 mm = 8 dots at 203 dpi_
