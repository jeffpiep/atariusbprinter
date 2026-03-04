# RP2040 Hardware Test Guide

Firmware is paused at `main()` by GDB. Work through each section in order.
All UART output appears on the Serial Monitor (115200 baud, GPIO0 TX).

---

## T1 — Boot and initialisation

**What to do:** `continue` in GDB (or `c`).

**Expected UART:**
```
[INFO][      0 ms][main_rp2040     ] USB Printer Driver RP2040 starting
[INFO][     xx ms][main_rp2040     ] Entering main loop
```

**Pass criteria:**
- Both lines appear within ~1 second of `continue`
- No hard fault / watchdog reset (no reboot loop in UART output)

**If missing:** Set a breakpoint on `tusb_init` and step through — a crash here
usually means the TinyUSB host stack didn't initialise (check USB OTG wiring).

---

## T2 — Protocol handler registration

**In GDB (before `continue`):**
```
break main_rp2040.cpp:163
continue
```
Step through the three `registerHandler` calls:
```
next   # TsplHandler registered
next   # EscpHandler registered
next   # PclHandler registered
print manager
```
Verify `m_handlers` vector has 3 entries (GDB will show the vector internals).

**Pass criteria:** `m_handlers` size = 3 after the third `next`.

---

## T3 — Text generator and SIO emulator init

Continuing from T2, step past the `makeTextGenerator` and `SioPrinterEmulator`
construction:
```
next   # makeTextGenerator(TSPL)
next   # generator->configure(cfg)
next   # SioUart sioUart / sioUart.init()
next   # SioPrinterEmulator sioEmulator
print tsplIdle
```

**Pass criteria:**
- `tsplIdle` == `true` (TSPL personality active)
- `mode` == `0` (COL_40 = 0)
- No crash / assertion failure during init

---

## T4 — USB printer mount (plug in the AiYin)

Let the firmware run freely (`continue`). Plug the AiYin (or any printer) into
the Pico's USB host port via an OTG adapter.

**Expected UART:**
```
[INFO][  xxxx ms][main_rp2040     ] USB device mounted: addr=1
[INFO][  xxxx ms][main_rp2040     ] addr=1: printer ready (epOut=0x01)
```

**GDB breakpoint alternative** — set before plugging in:
```
break tuh_mount_cb
break processPendingMount
```
When `tuh_mount_cb` fires, inspect:
```
print devAddr           # should be 1
print s_pendingMountAddr
```
When `processPendingMount` fires, step through descriptor parsing:
```
next                    # tuh_descriptor_get_configuration_sync
print ifaceNum          # -1 until printer interface found
# step through the while loop until ifaceNum >= 0
print ifaceNum          # should be 0
print epOut             # should be non-zero (e.g. 0x01)
print hasEpIn           # true if bidirectional (AiYin is bidirectional)
```

**Pass criteria:**
- `ifaceNum` >= 0
- `epOut` != 0
- `tuh_edpt_open` returns true
- "printer ready" message appears in UART

---

## T5 — Startup test label print

Immediately after T4 completes, `s_testPrintPending` is set and the main loop
calls `sendTestLabel`.

**Expected UART:**
```
[INFO][  xxxx ms][main_rp2040     ] Test label sent OK
```
**Expected hardware:** The AiYin prints a label reading "RP2040 Ready" / "USB OK".

**GDB breakpoint:**
```
break sendTestLabel
continue
```
Inside the function:
```
print job.rawData.size()   # should be > 0 (the TSPL string length)
next                       # manager.submitJob
```

**Pass criteria:**
- `submitJob` returns `true`
- "Test label sent OK" in UART
- Physical label prints correctly

---

## T6 — USB printer unmount (unplug)

Unplug the AiYin while firmware is running.

**Expected UART:**
```
[INFO][  xxxx ms][main_rp2040     ] USB device unmounted: addr=1
```

**GDB breakpoint:**
```
break tuh_umount_cb
```
When it fires:
```
print devAddr
```
Step through:
```
next   # Rp2040UsbTransport::onUnmount(devAddr)
next   # g_manager->onDetach(0, devAddr)
```

**Pass criteria:**
- `onUnmount` called with correct `devAddr`
- No crash after unplugging
- Re-plugging triggers T4 again (re-mount works)

---

## T7 — Re-mount after unplug

Re-plug the printer after T6. Full T4/T5 sequence should repeat cleanly:
mount → descriptor parse → endpoint open → test label.

**Pass criteria:** Identical to T4/T5. Verifies `Rp2040UsbTransport::s_devices`
slot is correctly recycled on re-use.

---

## T8 — Main loop cadence

With the printer plugged in and firmware running, verify the three main-loop
tasks are all being called. Set a temporary breakpoint in the loop body:

```
break main_rp2040.cpp:203   # tuh_task() line
commands
  silent
  continue
end
```
Let it run for a few seconds, then `Ctrl-C` and:
```
info breakpoints            # shows hit count
```

**Pass criteria:** hit count climbs continuously; firmware is not stuck anywhere.

---

## T9 — Non-printer USB device (negative test)

Plug in a **Full Speed** USB device that is NOT a printer (e.g. a USB flash
drive, USB serial adapter, or USB hub). **Do not use a USB mouse or keyboard** —
most are Low Speed (1.5 Mbps); the RP2040 USB host cannot enumerate Low Speed
devices without a hub, so `tuh_mount_cb` never fires and no log message appears.
Starting with a Low Speed device already connected may trigger a double-boot
(TinyUSB bus-reset artefact) — the firmware recovers cleanly on the second boot.

**Hardware note:** The Pico does not supply 5V on VBUS in host mode. Only
self-powered USB devices (printer with its own PSU, powered hub) will enumerate.
Bus-powered devices (flash drives, mice, keyboards) will not power up unless 5V
is supplied externally to the USB host connector's VBUS pin.

**Expected UART:**
```
[INFO][  xxxx ms][main_rp2040     ] USB device mounted: addr=1
[INFO][  xxxx ms][main_rp2040     ] addr=1: no printer-class interface with BULK OUT
```
No label should print.

**Pass criteria:**
- Mount callback fires
- `processPendingMount` exits early with the "no printer-class interface" message
- `s_testPrintPending` is NOT set (stays 0)
- No crash

---

## T10 — SIO emulator tick (smoke test)

The `sioEmulator.tick()` runs every main-loop iteration. Without an actual
Atari SIO bus connected, it should idle silently. Verify it isn't crashing:

```
break SioPrinterEmulator::tick
continue
```
Hit the breakpoint once, then:
```
delete <breakpoint number>
continue
```
Watch UART for 10 seconds — no spurious output or crash.

**Pass criteria:** No unexpected UART output; firmware continues running normally.

---

## Summary checklist

| # | Test | Pass |
|---|------|------|
| T1 | Boot messages appear | pass |
| T2 | 3 protocol handlers registered | pass |
| T3 | TSPL generator + COL_40 SIO emulator initialised | pass |
| T4 | Printer mounts, descriptor parsed, endpoints opened | pass |
| T5 | Test label prints physically | pass |
| T6 | Unmount handled cleanly | pass |
| T7 | Re-mount works | pass |
| T8 | Main loop running continuously | pass |
| T9 | Non-printer device rejected (Full Speed device) | pass |
| T10 | SIO tick idles without crash | pass |
