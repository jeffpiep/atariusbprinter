#!/usr/bin/env python3
"""escpos_font_demo.py — Render a custom OTF font and print a demonstration page
on an ESC/POS thermal printer (tested with POS80, VID 0483 PID 5720).

The demo page contains:
  1. The full printable character set (uppercase, lowercase, symbols)
  2. "The quick brown fox jumps over the lazy dog." (pangram)

The font is downloaded as ESC/POS user-defined characters (ESC &), replacing the
built-in ASCII charset for the duration of the job.

Usage:
    python tools/escpos_font_demo.py [options]

Options:
    --font FILE         OTF/TTF font file (default: Atari-822-Thermal.otf)
    --point-size-x N    Horizontal point size for X-axis canvas (default: 20)
    --point-size-y N    Vertical point size for 24-dot cell height (default: 22)
    --threshold N       Pixel brightness cutoff 0-255 (default: 50)
    --vid HEX           Printer USB vendor  ID in hex (default: 0483)
    --pid HEX           Printer USB product ID in hex (default: 5720)
    --tspl-print PATH   Path to tspl_print binary (default: ./build/tspl_print)
    --bin FILE          Also save the generated print-job binary to FILE

Prerequisites:
    pip install Pillow
    cmake -B build && cmake --build build -j$(nproc)
    /etc/udev/rules.d/99-tspl-printer.rules must grant rw access to the printer
"""

import argparse
import os
import subprocess
import sys
import tempfile

# ---------------------------------------------------------------------------
# Pull rendering helpers from otf_to_escpos.py (same tools/ directory)
# ---------------------------------------------------------------------------
_tools_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _tools_dir)

try:
    from otf_to_escpos import autofit_size, render_glyph, build_escand_sequence
except ImportError as e:
    print(f"Error: could not import otf_to_escpos.py from {_tools_dir}: {e}", file=sys.stderr)
    sys.exit(1)

try:
    from PIL import ImageFont
except ImportError:
    print("Error: Pillow not installed.  Run:  pip install Pillow", file=sys.stderr)
    sys.exit(1)

# ---------------------------------------------------------------------------
# ESC/POS constants
# ---------------------------------------------------------------------------
CELL_W         = 12
CELL_H         = 24
BYTES_PER_COL  = 3

ESC_INIT       = bytes([0x1B, 0x40])          # ESC @  — printer reset
ESC_CHARSET_ON = bytes([0x1B, 0x25, 0x01])    # ESC % 1 — activate user-defined charset
ESC_CHARSET_OFF= bytes([0x1B, 0x25, 0x00])    # ESC % 0 — restore built-in charset
ESC_FEED_4     = bytes([0x1B, 0x64, 0x04])    # ESC d 4 — feed 4 lines
GS_PARTIAL_CUT = bytes([0x1D, 0x56, 0x42, 0x00])  # GS V 66 0 — partial cut

# ---------------------------------------------------------------------------
# Build the demonstration print job
# ---------------------------------------------------------------------------

def build_demo_job(font_path: str, pt_x: int, pt_y: int, threshold: int) -> bytes:
    """Render the font and assemble a complete ESC/POS print job."""

    c1, c2 = 0x20, 0x7E
    num_chars = c2 - c1 + 1

    # --- Load font for Y-axis (height) ---
    try:
        font_y = ImageFont.truetype(font_path, pt_y)
    except Exception as e:
        print(f"Error loading font '{font_path}': {e}", file=sys.stderr)
        sys.exit(1)

    # --- Determine render canvas width from X point size ---
    if pt_x != pt_y:
        font_x = ImageFont.truetype(font_path, pt_x)
        render_w = max(
            font_x.getbbox(chr(c))[2] - font_x.getbbox(chr(c))[0]
            for c in range(c1, c2 + 1)
        )
        print(f"X canvas: pt_x={pt_x} → {render_w}px → crop to {CELL_W}")
    else:
        render_w = None  # default CELL_W, no crop

    print(f"Rendering {num_chars} glyphs  pt_y={pt_y}  threshold={threshold}  cell {CELL_W}x{CELL_H}")

    glyphs = [render_glyph(font_y, chr(c), threshold, render_w=render_w)
              for c in range(c1, c2 + 1)]

    esc_and_seq = build_escand_sequence(glyphs, c1, c2)

    # --- Assemble the complete job ---
    buf = bytearray()

    # 1. Printer reset
    buf += ESC_INIT

    # 2. Download user-defined character bitmaps
    buf += esc_and_seq

    # 3. Activate user-defined charset
    buf += ESC_CHARSET_ON

    # 4. Section header (built-in charset — will print before ESC%1 takes effect
    #    on the *next* line, so we keep headers after activation)
    #    Print a banner line in the user font to confirm it loaded
    buf += b"=== ESC/POS Custom Font Demo ===" + bytes([0x0A])

    # 5. Full character set — three lines
    buf += b"Uppercase: ABCDEFGHIJKLMNOPQRSTUVWXYZ" + bytes([0x0A])
    buf += b"Lowercase: abcdefghijklmnopqrstuvwxyz" + bytes([0x0A])
    buf += b"Symbols:   0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~" + bytes([0x0A])

    # 6. Blank separator line
    buf += bytes([0x0A])

    # 7. Pangram — exercises every letter of the alphabet
    buf += b"The quick brown fox jumps over the lazy dog." + bytes([0x0A])

    # 8. Second pangram variant for extra coverage
    buf += b"Pack my box with five dozen liquor jugs." + bytes([0x0A])

    # 9. Restore built-in charset
    buf += ESC_CHARSET_OFF

    # 10. Feed and partial cut
    buf += ESC_FEED_4
    buf += GS_PARTIAL_CUT

    return bytes(buf)


# ---------------------------------------------------------------------------
# Send to printer via tspl_print --escpos
# ---------------------------------------------------------------------------

def send_to_printer(job: bytes, tspl_print: str, vid: int, pid: int) -> bool:
    """Write job to a temp file and send via tspl_print --escpos."""
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(job)
        tmp = f.name

    try:
        cmd = [tspl_print, "--escpos",
               "--vid", f"{vid:04x}",
               "--pid", f"{pid:04x}",
               tmp]
        print(f"Sending {len(job)} bytes → {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=False)
        if result.returncode != 0:
            print(f"tspl_print exited with code {result.returncode}", file=sys.stderr)
            return False
        print("Job sent successfully.")
        return True
    finally:
        os.unlink(tmp)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Render OTF font and print a character-set + pangram demo on ESC/POS printer."
    )
    parser.add_argument("--font", default="Atari-822-Thermal.otf",
                        help="OTF/TTF font file (default: Atari-822-Thermal.otf)")
    parser.add_argument("--point-size-x", type=int, default=20, metavar="N",
                        help="Horizontal point size — canvas width before 12-dot crop (default: 20)")
    parser.add_argument("--point-size-y", type=int, default=22, metavar="N",
                        help="Vertical point size — height in 24-dot cell (default: 22)")
    parser.add_argument("--threshold", type=int, default=50, metavar="N",
                        help="Pixel brightness cutoff 0-255 (default: 50)")
    parser.add_argument("--vid", default="0483",
                        help="Printer USB vendor ID hex (default: 0483 = STM32/POS80)")
    parser.add_argument("--pid", default="5720",
                        help="Printer USB product ID hex (default: 5720 = POS80)")
    parser.add_argument("--tspl-print", default="./build/tspl_print", metavar="PATH",
                        help="Path to tspl_print binary (default: ./build/tspl_print)")
    parser.add_argument("--bin", metavar="FILE",
                        help="Also save the generated binary to FILE")
    args = parser.parse_args()

    # Resolve font path relative to project root (one level up from tools/)
    font_path = args.font
    if not os.path.isabs(font_path) and not os.path.isfile(font_path):
        candidate = os.path.join(os.path.dirname(_tools_dir), font_path)
        if os.path.isfile(candidate):
            font_path = candidate
    if not os.path.isfile(font_path):
        print(f"Error: font not found: {font_path}", file=sys.stderr)
        sys.exit(1)

    # Verify tspl_print exists
    tspl = args.tspl_print
    if not os.path.isfile(tspl):
        print(f"Error: tspl_print not found at '{tspl}'", file=sys.stderr)
        print("Build it first:  cmake -B build && cmake --build build -j$(nproc)", file=sys.stderr)
        sys.exit(1)

    vid = int(args.vid, 16)
    pid = int(args.pid, 16)

    # Build job
    job = build_demo_job(font_path, args.point_size_x, args.point_size_y, args.threshold)

    # Optionally save binary
    if args.bin:
        with open(args.bin, "wb") as f:
            f.write(job)
        print(f"Saved binary: {args.bin}  ({len(job)} bytes)")

    # Send to printer
    success = send_to_printer(job, tspl, vid, pid)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
