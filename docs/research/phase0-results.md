# Phase 0 results

Hardware checks from implementation plan phase 0, measured by the owner with the measurement build `hwtest/dx-measure`. This file fills in as the steps are done. Step 0.8 then draws the conclusions and proposes design changes.

**Setup:** a Pico 2 W (SSID suffix C2C4).

## 0.2 Bench (2026-11-08, USB power, no console)

**Builds:** `a0a22b1` (first install), `b4b8d0d` (DHCP fix), `97d31f6` (D8 rule, reboot buttons).

| Check | Result |
|---|---|
| Access point, captive portal | Works. The phone opened the report page by itself, once DHCP worked (see below) |
| DHCP | Failed in `a0a22b1`; fixed in `b4b8d0d`. With `LWIP_DHCP` off, lwIP also leaves `IP_ACCEPT_LINK_LAYER_ADDRESSING` off and drops every packet from `0.0.0.0`, which includes every DHCP DISCOVER |
| Cold boot (USB replug) | POR, `CHIP_RESET` = `0x00010000`, session marker not set, injection attempted, "no CS" (no console) |
| Warm reboot, `watchdog_reboot()` | `CHIP_RESET` still `0x00010000`; marker set; skipped |
| Warm reboot, `rom_reboot()` | `CHIP_RESET` still `0x00010000`, watchdog reason `0x1`; marker set; skipped |
| `main()` to PIO ready | 295–301 µs (6 boots) |
| Timer at `main()` | 112 µs after a cold boot, 5363 µs after a ROM reboot |
| Wi-Fi up | 0.79–0.80 s after the injection window ended |
| Download (Tx load) | 800–950 KB/s |
| Last flash page `0x3FFF00` after a UF2 install | 256 × `0xEF`: picotool's RP2350-E10 block landed there |

**What this means (to be confirmed in step 0.8):**

1. **`HAD_POR` survives warm reboots** (risk R6).
   - Neither the watchdog nor the ROM reboot sets a new bit in `CHIP_RESET`, and HAD_POR stays set.
   - `CHIP_RESET` alone can't tell a warm reboot from a power-on.
   - The POWMAN scratch session marker caught both reboots. Design D8's two-signal rule is needed, and it works on hardware.
2. **The E10 block overwrites the end of flash.**
   - Every USB install of a picotool-built RP2350 UF2 (PicoBoot-DX, our own builds, pico-examples) carries an absolute-family block for `0x10FFFF00`. On the Pico 2 W's 4 MB flash it lands in the last page, so the last sector, `0x3FF000`–`0x400000`, is overwritten.
   - In layout D6 that is the end of payload 2. The layout has to keep that sector free, and our own install UF2 can leave the block out.
3. **Throughput is probably limited by lwIP's send buffer**, not the radio [inference].
   - `TCP_SND_BUF` is 8 × MSS = 11.7 KB; at a Wi-Fi round trip of 10–15 ms that allows about 0.8–1.2 MB/s, which matches.
   - Fast enough for OTA (a 256 KB firmware in under a second, Swiss in about 1.5 s).
   - To tune in phase 2.

## 0.3 Cold boots in the console (2026-11-08)

**Setup:**
- DOL-001 with SD2SP2 and HDMI adapter; Pico fed through 3V3.
- Build `97d31f6`.
- Boots 1–10 with Swiss r2119 from flash, boots 11–20 with the built-in gekkoboot, boot 21 a watchdog reboot while a game ran.

| | Swiss r2119 (boots 1–10) | gekkoboot (boots 11, 13–20) | Boot 12 (gekkoboot) |
|---|---|---|---|
| Result | 10/10 booted | 9/9 booted | **Blue screen** |
| Margin (PIO ready → first CS) | 164.0–166.6 ms | 165.8–167.3 ms | **44.2 ms** |
| Injection (first CS → end) | 406 ms | 19 ms | **142 ms** |
| CS at the end / expected | 1544 / 1543 | 285 / 284 | **286** / 284 |
| `main()` → PIO ready | 300–314 µs | 303–312 µs | 307 µs |

**Warm reboot in a running game (boot 21):**
- The game kept running, and the boot was skipped through the session marker.
- `CHIP_RESET` was again `0x00010000`, with watchdog reason `0x2` (FORCE).

**Findings:**

1. **The margin is large and steady:** about 166 ms from "PIO ready" to the console's first IPL read, ±1.5 ms over 19 boots. A loader that costs a few milliseconds (D10) doesn't come close, on this console at least.
2. **The EXI rate:** Swiss's 1259 extra 1 KB transfers took 387 ms more than gekkoboot's, so about 0.31 ms per transfer (≈3.3 MB/s). The 224 skipped transfers take well under a millisecond.
3. **One extra CS pulse after the injection,** on every good boot: the console's next access, right after the payload.
4. **Boot 12 shows how PicoBoot boots fail** [inference from the numbers].
   - The console's real IPL read came at the normal time: 44.2 + 142 = 186.2 ms, against 166 + 19 ≈ 185.6 ms on the good boots.
   - But one extra CS falling edge came about 122 ms earlier.
   - `on_transfer` counts the 224 skipped pulses without checking them, so that one spurious pulse made the payload go out one transfer early. The console then got a shifted payload and crashed.
   - PicoBoot-DX and upstream PicoBoot use the same program, so they have the same weakness.
   - **Rate:** 1 in 20 boots here.
   - **Not new:** boot 12 was a normal cold boot. The owner has seen the same occasional blue screens on this console with PicoBoot-DX v0.9 and earlier, and with upstream PicoBoot.
   - **Possible causes** (not known yet): the console's power-up, the HDMI adapter, or noise. One suspect: the RP2350's GPIOs leave reset with their pull-downs on, and PicoBoot never changes them. If the console doesn't drive CS during part of its power-up, the Pico holds it low, and a brief drive high reads as an extra falling edge. Build `55ad372`+ records the CS level at "PIO ready" to test this.
   - **To check:** a CLK-qualified count would ignore such a pulse, because a real EXI transfer always clocks. A second passive counter, added in the next measurement build, tests that on hardware before the injection changes.

## 0.4 Warm board

*To do.*

## 0.5 Current

*To do.*

## 0.6 Range

*To do.*

## 0.7 Stress test

*To do.*
