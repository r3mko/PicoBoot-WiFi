# Phase 1 results

Hardware tests from implementation plan phase 1, run by the owner on the `phase-1-core` builds.

**Setup:** the Pico 2 W from phase 0 (SSID suffix C2C4). The page reports it as an **RP2350 A2** (boot ROM version 2), so RP2350-E9 applies to it.

## Bench check (2026-11-08, USB power, no console)

The "done when" of steps 1.6–1.9: install the USB install UF2 and check that the loader chains slot A and the report shows warm and power-on boots.

| Build | Result |
|---|---|
| `6760aee` / `5540be7` | **Nothing happened:** no LED, no Wi-Fi. The loader hung before `main()` (bug 1 below) |
| `eda0fae` | Works: loader → slot A, Wi-Fi blob from Wi-Fi A, AP up, power-on rule right, boot log and buttons work. But Wi-Fi took 4.4–6.2 s to come up |
| `772fa75` | Shows why: the chained firmware read flash in the boot ROM's slowest mode (bug 2) |
| **`1a70573`** | **Bench check passed** |

**`1a70573` in detail:**

| Check | Result |
|---|---|
| Start-up path | "the loader, slot A; boot control empty (defaults)" |
| Power-on rule (D8) | USB plug-ins: POR, power-on, injection attempted ("no CS", no console). Watchdog reboot: warm, not run |
| CS at "PIO ready" | high (the D36 pull-up) |
| `main()` → PIO ready | 321–326 µs |
| Wi-Fi blob (D14) | From Wi-Fi A; AP up 795 ms after start-up (CYW43 init 0 ms, AP start 789 ms) |
| Flash read mode | `0xEB` quad I/O, 16 dummy bits, divider 3 (41 MHz at 125 MHz; 83 MHz during the injection) |
| Payload DMA speed through the uncached window | **18.15 MB/s** at 125 MHz; the console needs about 3.3 MB/s |
| Boot log (D39) | Lists every boot, warm ones included |
| Payload buttons | "Next power-on" follows the buttons |

**Bugs found and fixed:**

1. **The loader hung before `main()`** (`eda0fae`).
   - The loader skips the SDK's clock setup. The next start-up step, `runtime_init_post_clock_resets()`, takes every peripheral out of reset and waits; peripherals on clocks the boot ROM leaves off never finish.
   - Fix: the loader skips that step too (`PICO_RUNTIME_SKIP_INIT_POST_CLOCK_RESETS`).
2. **The chained firmware read flash in the boot ROM's slowest mode** (`1a70573`).
   - Mode: serial `0x03` at `clk_sys`/256, **0.06 MB/s**, about 55 times too slow for the payload DMA. On RP2350 the SDK only sets up fast reads if an XIP setup (boot2) is embedded, and after every flash write it restores the boot RAM copy.
   - Fix: the firmware embeds `boot2_w25q080` (`PICO_EMBED_XIP_SETUP`) with divider 3 (design D27): quad I/O continuous read, 83 MHz flash clock during the injection.

**Also added for debugging:** `PICOBOOT_WIFI_USB_TRACE`, a firmware build that prints its start-up steps over USB serial. It wasn't needed in the end.

## 1.10 Console tests

**Setup:**
- DOL-001 with SD2SP2 and HDMI adapter; Pico fed through 3V3 as in phase 0.
- Build `1a70573`: the loader chains slot A.

### Part A: cold boots (2026-11-08)

| | Phase 0 (PicoBoot-DX, booted by the ROM) | Phase 1 (loader → slot A) |
|---|---|---|
| Swiss r2119, 10 boots | margin 164.0–166.6 ms, mean 165.5 ms | **10/10**, margin 161.5–163.6 ms, **mean 162.9 ms**; CS 1544/1544 |
| gekkoboot, 10 boots | margin 165.8–167.3 ms, mean 166.8 ms | **10/10**, margin 159.7–163.0 ms, **mean 162.1 ms**; CS 285/285 |
| `main()` → PIO ready | ~300 µs | 312 µs |
| Injection time (gekkoboot) | 19 ms | 19 ms |

**Findings:**

1. **The loader path costs about 3–5 ms** of margin: 2.6 ms for Swiss, 4.7 ms for gekkoboot. Phase 0's own series varied by about 2 ms from day to day. More than 158 ms of margin remains (design D10).
2. **Swiss injects completely through the uncached flash window,** with the quad XIP setup at 83 MHz.
3. **No glitches with the CS pull-up:** raw = clocked on all 20 boots.

### Part B: warm board (2026-11-08)

**Setup:** Swiss r2119 from payload 1; 30 minutes of play with the console closed (boot 27), then 5 power cycles with about 5 s off (boots 28–32).

| | Result |
|---|---|
| Boots | **6/6** good |
| Margin | 159.3–162.7 ms (cold, part A: 161.5–163.6 ms) |
| CS raw / clocked | 1544 / 1544 on every boot |
| Injection | complete, 406 ms |
| `main()` → PIO ready | up to 347 µs |
| RP2350 temperature reading | 21.5 °C (reads low on this console's rail, see phase 0 §0.5) |

**Finding:** no warm-board effect, as in phase 0. As in phase 0, the board didn't get very warm in this console.

### Part C: warm reboot during a game (2026-11-08)

*Reboot (watchdog)* while a Swiss game ran: boot 33 was logged as "warm (not run)", so no injection; the game kept running. Design D8 holds with the loader, as in phase 0.

### Part D: glitch check, first try (2026-11-08): not a valid test

**Setup:** the pull-down test mode, boots 34–53 (20 boots).

**Result:** 20/20 good, raw = clocked. But CS read **high** at "PIO ready" on every boot, where PicoBoot's setting reads low (phase 0, every boot). So the glitch condition never occurred.

**Cause:** the test mode first set the pull-up, as normal, and then switched to the pull-down.
- On this RP2350 A2, erratum RP2350-E9 then holds the released pin at about 2.2 V, which reads high: with the input enabled, a pin passing through the undefined range picks up a leakage current the pull-down can't overcome.
- PicoBoot never touches the pulls. Its CS stays pulled down from reset, and its input is enabled while the pin is already low, which E9 doesn't disturb.

**Fix (`ddcca92`):** the test mode now leaves the pad exactly as reset left it. Part D is to be repeated with that build; CS should then read low at "PIO ready".

**Side finding for D36:** on A2 chips, any code that switches CS from the pull-up to the pull-down doesn't get a pull-down. The D36 pull-up itself is unaffected: "the pad pull-up still works" (datasheet).

### Part D: glitch check, batch 1 (2026-11-08)

**Setup:**
- Build `023c430`: the test mode leaves CS as reset left it.
- Swiss r2119 from payload 1.
- Boot 55 ran in the normal pull-up mode; boots 56–74 (19 boots) ran in the pull-down test mode.

| | Result |
|---|---|
| CS at "PIO ready" in test mode | **low** on all 19 boots, as with PicoBoot (phase 0), so the test is faithful now |
| Boots | **19/19** good from the Pico's side (injection complete) |
| **Boot 68** | **GLITCH survived:** first raw CS 37.1 ms after PIO ready, raw 1545 / clocked 1544, injection complete |
| Other boots | margin 161.3–163.4 ms, 1544/1544 |
| Boot 54 | USB-powered with the console off: CS held low by the unpowered console, no CS. Harmless |

**Finding:**
- With PicoBoot's pad setting, the false CS edge at power-up happened again (boot 68: 37.1 ms; phase 0: 42.0 and 44.2 ms).
- The clock-qualified skip count (D36) didn't count it: the injection started at the real 225th transfer and completed. In phase 0 the same event gave a blue screen.
- *Console side to confirm with the owner: Swiss started on boot 68.*
