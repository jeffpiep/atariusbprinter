#!/usr/bin/env bash
# escpos_timeout_probe.sh — find the POS80 printer's idle-feed timeout threshold.
#
# Sends pairs of lines with increasing delays between them.
# Look for where the idle feed-gap (and possible auto-cut) first appears.
# That delay is the printer's idle timeout threshold.
#
# Run from project root:
#   bash docs/testing/escpos_timeout_probe.sh

VID=0483
PID=5720
CMD="./build/tspl_print --escpos --vid $VID --pid $PID -"

for ms in 000 10 20 30 40 50 60 70 80 90 100; do
    echo "--- delay ${ms}ms ---"
    printf "BEFORE %4dms\\x0a" "$ms" | $CMD
    sleep "$(echo "scale=3; $ms/1000" | bc)"
    printf "AFTER  %4dms\\x0a" "$ms" | $CMD
done

echo "Done. Note which delay first shows a feed gap between BEFORE and AFTER lines."
