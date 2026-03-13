#!/usr/bin/env bash
# ESC/POS line-printing tests — simulates Atari LPRINT behavior.
# Each printf | tspl_print call is one USB bulk transfer (one "job").
# No init (ESC @), no feed (ESC d), no cut (GS V).

VID=0483
PID=5720
CMD="./build/tspl_print --escpos --vid $VID --pid $PID -"


# printf '\x1b2LINE ONE\x0a' | $CMD
# printf 'LINE TWO\x1bK' | $CMD
# printf 'LINE THREE\x0a' | $CMD


# ── Test 1: single line, no spacing command ───────────────────────────────────
echo "Test 1: single line, no spacing command"
printf '\x1b@LINE ONE\x0a' | $CMD
#sleep 1

# ── Test 2: single line with ESC 3 30 (6 LPI) ────────────────────────────────
echo "Test 2: single line with ESC 3 30"
printf '\x1b\x33\x1e LINE TWO ESC3\x0a' | $CMD
#sleep 1

# ── Test 3: three lines, each a separate USB transfer (Atari LPRINT loop) ────
echo "Test 3: three lines as separate USB transfers"
printf '\x1b\x33\x1e LINE 1\x0a' | $CMD
printf '\x1b\x33\x1e LINE 2\x0a' | $CMD
printf '\x1b\x33\x1e LINE 3\x0a' | $CMD
#sleep 1

# ── Test 4: three lines in a single USB transfer (for comparison) ─────────────
echo "Test 4: three lines in one USB transfer"
printf '\x1b\x33\x1e LINE A\x0a\x1b\x33\x1e LINE B\x0a\x1b\x33\x1e LINE C\x0a' | $CMD
#sleep 1

# ── Test 5: blank line between two text lines (LPRINT "") ─────────────────────
echo "Test 5: blank line between two text lines (separate transfers)"
printf '\x1b\x33\x1e TOP\x0a' | $CMD
printf '\x1b\x33\x1e\x0a' | $CMD
printf '\x1b\x33\x1e BOTTOM\x0a' | $CMD

# ── Test 6: spacing value comparison ──────────────────────────────────────────
# Each block uses a different spacing command on every line (separate USB transfers).
# The separator "----------" is sent without any spacing command so it uses the
# printer's current state — useful as an additional visual indicator.
echo "Test 6: spacing comparison (ESC 2 vs ESC 3/30 vs ESC 3/60 vs default)"
# Block A: ESC 2 — select default 1/6-inch spacing (2-byte command)
printf '\x1b\x32 A ESC2  LINE1\x0a' | $CMD
printf '\x1b\x32 A ESC2  LINE2\x0a' | $CMD
printf '\x1b\x32 A ESC2  LINE3\x0a' | $CMD
printf '= = = = = =\x0a' | $CMD
# Block B: ESC 3 30 — current code (30 units × y-motion-unit)
printf '\x1b\x33\x1e B ESC3/30 L1\x0a' | $CMD
printf '\x1b\x33\x1e B ESC3/30 L2\x0a' | $CMD
printf '\x1b\x33\x1e B ESC3/30 L3\x0a' | $CMD
printf '= = = = = =\x0a' | $CMD
# Block C: ESC 3 60 — 1/6 inch if y=1/360 inch (60 units × 1/360 = 1/6 inch)
printf '\x1b\x33\x3c C ESC3/60 L1\x0a' | $CMD
printf '\x1b\x33\x3c C ESC3/60 L2\x0a' | $CMD
printf '\x1b\x33\x3c C ESC3/60 L3\x0a' | $CMD
printf '= = = = = =\x0a' | $CMD
# Block D: no spacing command — printer uses whatever state it currently has
printf ' D DEFAULT L1\x0a' | $CMD
printf ' D DEFAULT L2\x0a' | $CMD
printf ' D DEFAULT L3\x0a' | $CMD

echo "Done."
