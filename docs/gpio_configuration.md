# GPIO Configuration — SIO UART and Command Line

There are **two places** in the code that control GPIO assignments, working as a
two-level default/override system.

---

## Level 1 — Default constant (protocol layer)

**`include/sio/SioProtocol.h:20`**

```cpp
constexpr uint32_t CMD_LINE_GPIO = 26;    // default GPIO for SIO command line
```

This is the canonical default for the command-line GPIO. It lives in the protocol
namespace so it can be referenced from any layer. Change this only if you want to
update the project-wide default — every `Config` that uses `Sio::CMD_LINE_GPIO`
as its `cmdPin` initialiser will pick up the new value automatically.

---

## Level 2 — Config struct defaults (platform layer)

**`platform/rp2040/SioUart.h:15-21`**

```cpp
struct Config {
    uart_inst_t* uart     = uart1;              // UART peripheral (uart0 or uart1)
    uint         txPin    = 4;                  // GPIO for UART TX (SIO data out)
    uint         rxPin    = 5;                  // GPIO for UART RX (SIO data in)
    uint         cmdPin   = Sio::CMD_LINE_GPIO; // GPIO 26 — SIO command line
    uint32_t     baudRate = Sio::BAUD_RATE;     // 19200
};
```

These are the defaults applied whenever a `Config` is default-constructed.
Changing them here changes every build that doesn't explicitly override them in
`main_rp2040.cpp`.

---

## Level 3 — Per-build override (entry point)

**`platform/rp2040/main_rp2040.cpp:188-189`** — the **recommended single place**
to change GPIO assignments for a specific hardware build:

```cpp
SioUart::Config sioCfg;      // starts with all defaults
// ── Override here for your specific wiring ────────────────────────
// sioCfg.uart    = uart0;   // use UART0 instead of UART1
// sioCfg.txPin   = 12;      // GP12 → SIO data (to Atari)
// sioCfg.rxPin   = 13;      // GP13 ← SIO data (from Atari)
// sioCfg.cmdPin  = 22;      // GP22 ← SIO command line
// ─────────────────────────────────────────────────────────────────
static SioUart sioUart(sioCfg);
```

Uncomment and set the fields that differ from the defaults. The `SioUart`
constructor stores the `Config` and uses all four fields during `init()` — you
only need to touch `main_rp2040.cpp` for any hardware-specific wiring.

---

## RP2040 UART↔GPIO pairing constraints

The RP2040 routes UART signals through fixed GPIO banks. Both TX and RX must be
on pins assigned to the same UART peripheral. Valid pairs:

| Peripheral | TX options          | RX options          |
|------------|---------------------|---------------------|
| `uart0`    | GP0, GP12, GP16, GP28 | GP1, GP13, GP17, GP29 |
| `uart1`    | GP4, GP8, GP20, GP24  | GP5, GP9, GP21, GP25  |

The defaults (`uart1`, TX=GP4, RX=GP5) match the first valid pair for UART1.
The command-line GPIO (`cmdPin`) is a plain input with an interrupt — any free
GPIO works.

---

## Summary

| What to change                          | Where                                                    |
|-----------------------------------------|----------------------------------------------------------|
| Project-wide default command-line GPIO  | `include/sio/SioProtocol.h` — `CMD_LINE_GPIO`           |
| Project-wide default UART/TX/RX pins    | `platform/rp2040/SioUart.h` — `Config` member initialisers |
| Per-build wiring for a specific PCB     | `platform/rp2040/main_rp2040.cpp` — populate `sioCfg` fields |
