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

## 0.3 Cold boots in the console

*To do.*

## 0.4 Warm board

*To do.*

## 0.5 Current

*To do.*

## 0.6 Range

*To do.*

## 0.7 Stress test

*To do.*
