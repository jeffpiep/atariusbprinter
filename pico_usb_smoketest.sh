#!/bin/bash
# Build, flash, and start a GDB debug session for the usbprinter_usb_test firmware.
# No SIO — tests the USB write path to the POS80 thermal printer in isolation.
# Serial output (LOG_INFO timing lines) appears on UART (GPIO 0/1, 115200 baud).
# Usage: ./pico_usb_smoketest.sh

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PICO_DIR="$REPO_ROOT/pico/usbprinter"
BUILD_DIR="$PICO_DIR/build"
ELF="$BUILD_DIR/usbprinter_usb_test.elf"
SDK="$HOME/.pico-sdk"
OPENOCD="$SDK/openocd/0.12.0+dev/openocd"
OPENOCD_SCRIPTS="$SDK/openocd/0.12.0+dev/scripts"
GDB="$SDK/toolchain/14_2_Rel1/bin/arm-none-eabi-gdb"

# ── Build ─────────────────────────────────────────────────────────────────────
echo "=== Build ==="
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cmake -B "$BUILD_DIR" -S "$PICO_DIR"
fi
cmake --build "$BUILD_DIR" --target usbprinter_usb_test -- -j"$(nproc)"

# ── Flash ─────────────────────────────────────────────────────────────────────
echo "=== Flash ==="
"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -f interface/cmsis-dap.cfg \
    -f target/rp2040.cfg \
    -c "adapter speed 5000; program \"$ELF\" verify reset exit"

# ── Debug ─────────────────────────────────────────────────────────────────────
echo "=== Debug (OpenOCD :3333 + GDB) ==="

"$OPENOCD" \
    -s "$OPENOCD_SCRIPTS" \
    -f interface/cmsis-dap.cfg \
    -f target/rp2040.cfg \
    -c "adapter speed 5000" \
    2>/dev/null &
OPENOCD_PID=$!

trap 'kill $OPENOCD_PID 2>/dev/null; wait $OPENOCD_PID 2>/dev/null' EXIT

sleep 0.5   # let OpenOCD bind its ports

"$GDB" "$ELF" \
    -ex "set pagination off" \
    -ex "target remote localhost:3333" \
    -ex "monitor reset init" \
    -ex "load" \
    -ex "break main" \
    -ex "continue"
