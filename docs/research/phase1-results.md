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
