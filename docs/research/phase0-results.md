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

## 0.3b Glitch series (2026-11-08)

### Part 1: the CS pull-up

**Setup:** build `0182c06`, board in position B, Swiss r2119 from flash, *Next boots: CS pull-up*, 5 cold boots.

| | Pull-down (PicoBoot default; 0.3–0.7) | Pull-up (5 boots) |
|---|---|---|
| CS at "PIO ready" | **low**, first driven high ~41.7 ms later | **high** on every boot |
| Result | ok (boot 12 aside) | **5/5** ok |
| Margin | 164.0–168.5 ms | 167.4–168.0 ms |
| CS / clocked count | 1544 / 1544 | 1544 / 1544 |
| `main()` → PIO ready | 300–317 µs | 317–319 µs |

**Findings:**

1. **Nobody drives CS during the console's first ~42 ms.**
   - With a pull-up the line reads high, so the low level seen before came from the Pico's own reset pull-down, not from the console. This is explanation (b) from 0.7.
   - Boot 12's extra falling edge, 2.5 ms after the console first drove CS, was most likely a moment the console's driver let go of the line. The pull-down then pulled CS low [inference].
2. **The pull-up doesn't change the injection:** same margin, same counts, all boots good. It costs about 17 µs before "PIO ready" (code fetched from flash), against a 167 ms margin.
3. **Candidate fixes for phase 1, independent of each other:**
   - a pull-up on CS, which prevents the glitch;
   - a CLK-qualified count of the 224 skipped pulses, which ignores one that gets through.
   - Parts 2 and 3 compare glitch rates with the pull-down and the pull-up.
4. **Background: why PicoBoot pulls CS down.**
   - **No deliberate choice:** neither upstream PicoBoot (`src/pio.c`) nor PicoBoot-DX sets a pull. `pio_gpio_init()` → `gpio_set_function()` leaves the pad pulls alone ("This doesn't affect e.g. pullup/pulldown", SDK `gpio.c`).
   - **So the reset default stays:** pull-down on, pull-up off, on both RP2040 and RP2350 (`PADS_BANK0_GPIO0_PDE_RESET = 1`, `PUE_RESET = 0`).
   - **Strength:** the pulls are weak (RP2350 at 3.3 V: pull-up 32–86 kΩ, pull-down 36–113 kΩ, datasheet p.1337) and only set the level of a line nobody drives.
   - **What it means for CS:** for an active-low chip select, the pull-down makes an undriven CS read as "selected". It also selects the real IPL chip (U10) while the console powers up.
   - **RP2350-E9 (A2 chips only; fixed in A3, datasheet Appendix D.5.1):** with the input enabled, a floating pad can hold at about 2.2 V through a ~120 µA leakage that the pull-down can't overcome. An undriven CS can then read either way. "The pad pull-up still works."
   - **The owner's chip:** its stepping is unknown so far.
   - **Documentation:** none found on what the GameCube does with CS during power-up. The measurements here are the evidence.
5. **The temperature reading dropped to 12.2 °C** after the board was moved and rewired. Since the reading tracks the rail voltage (see 0.4–0.5), the 3V3 at the Pico may now be a little lower. Worth a quick check with the meter.

### Part 3: glitch rate with the pull-up

**Setup:** build `0182c06`, *CS pull-up*, Swiss r2119 from flash, a fresh run of 25 cold boots (log cleared after part 1).

| | Result |
|---|---|
| Boots | **25/25** good, none with a GLITCH row; with part 1, **30/30** with the pull-up |
| CS at "PIO ready" | high on every boot |
| Margin | 165.4–168.1 ms |
| CS / clocked count | 1544 / 1544 on every boot |
| `main()` → PIO ready | 316–327 µs |

**Pull-down boots so far** (0.3, 0.4, 0.7): 1 glitch in 31 cold boots (boot 12 of 0.3).

**Limits of this result:**
- 0 in 30 supports the pull-up, but doesn't prove it: at a rate of 1 in 20, a clean run of 30 still happens about 21% of the time.
- Part 2 (pull-down) adds the comparison, and a chance to see the clocked counter catch a glitch on hardware.

### Part 2: glitch rate with the pull-down (PicoBoot's default)

**Setup:** build `0182c06`, *CS pull-down*, Swiss r2119 from flash, a fresh run of 25 cold boots.

| | Result |
|---|---|
| Boots | 24 ok; **boot 2: GLITCH** |
| CS at "PIO ready" | low on every boot, first driven high 40.9–42.x ms later |
| Boot 2 | First raw CS **42.0 ms** after PIO ready, right as CS was first driven high (`low>42ms`). Raw count **1545**, clocked count **1544**: "GLITCH: 1 unclocked CS". Injection 532 ms instead of 406 ms (it started counting at the false edge) |
| Other boots | Margin 166.0–168.2 ms, raw = clocked = 1544 |

### Conclusions of 0.3b

| CS pull | Cold boots | Glitches |
|---|---|---|
| Down (PicoBoot default; 0.3, 0.4, 0.7, part 2) | 56 | **2** (0.3 boot 12, part 2 boot 2) |
| Up (parts 1 and 3) | 30 | **0** |

1. **The clock-qualified counter catches the glitch on hardware.**
   - It ignored the one false CS pulse of part 2's boot 2.
   - It agreed with the raw count on every one of the 80+ good boots, so it doesn't miss real transfers.
   - A CLK-qualified count of the 224 skipped pulses would have avoided both failures.
2. **The glitch is tied to the console's power-up.** Both happened 42–44 ms after "PIO ready", as the console first drove CS. Until then the line is undriven, and PicoBoot's reset pull-down holds it low.
3. **The pull-up removes the undriven-low state** (CS reads high from the start). It had no failures in 30 boots, against 2 in 56 with the pull-down.
   - As statistics that's thin (all 2 failures landing in the pull-down group has a probability of about 0.42 by chance). The mechanism supports it.

## 0.4 Warm board (2026-11-08)

**Setup:**
- Build `97d31f6`, Swiss r2119 from flash.
- 30 minutes of play with the console closed, then 5 power cycles with about 5 s off.

| | Cold (0.3, Swiss) | Warm (0.4, Swiss) |
|---|---|---|
| Result | 10/10 | **5/5** |
| Margin | 164.0–166.6 ms | 167.4–168.5 ms |
| Injection | complete, 406 ms | complete, 406–407 ms |
| CS at the end / expected | 1544 / 1543 | 1544 / 1543 |
| `main()` → PIO ready | 300–314 µs | 303–305 µs |
| RP2350 temperature reading | 18.7 °C | 20.6 °C |

**Findings:**

1. **No warm-board trouble,** and the margin barely moved.
2. **The board hardly warmed up.** The reading rose only 2 °C, so this wasn't the hot board of upstream #148. A second warm test follows the 0.7 stress test, when the radio has been transmitting for minutes.
3. **The 3.3 V rail may be low in the console** [inference].
   - The RP2350 read 34.6–35.6 °C on the bench (USB) but 18.7–20.6 °C in the console. A console interior isn't 15 °C colder than the room.
   - The ADC measures against the board's own 3.3 V, and the conversion assumes 3.30 V; a lower supply reads as a lower temperature.
   - If the die is about as warm in both places, the readings fit about **3.18 V** in the console.
   - The CYW43439 guarantees full RF performance only above 3.2 V (see [hardware.md](hardware.md) §1).
   - To measure with a meter in step 0.5.

## 0.5 Voltage and current (2026-11-08)

**Setup:** voltage between the Pico's pin 36 (3V3) and pin 38 (GND), measured with a multimeter, with the board wired as normal (3V3 from the console).

| State | 3V3 at the Pico |
|---|---|
| Radio off (Wi-Fi 60 s late) | 3.21 V |
| AP idle, no client | 3.16 V |
| Client connected, idle | 3.15 V |
| Download running | 3.15 V |

**Current:** not measured. The owner's setup doesn't allow a meter in series with the Pico's 3.3 V wire. The design uses the datasheet figures instead ([hardware.md](hardware.md) §1): about 40 mA for the radio listening, Tx bursts up to about 320 mA at full power, and 20–30 mA for the RP2350.

**Findings:**

1. **The console's 3.3 V rail is low at the Pico:** 3.21 V even with the radio off. That confirms the inference from the temperature readings in 0.4 (about 3.18 V).
2. **With the radio on, it averages 3.15–3.16 V.** That's below the 3.2 V where the CYW43439 guarantees full RF performance, though above its 3.0 V minimum.
3. **Short dips during Tx bursts** can't be seen with a meter. The 0.7 stress test checks for their effects (brown-outs, Wi-Fi drops, console glitches).
4. **Design consequence to consider in 0.8:** recommend, or require, the 5 V-to-VSYS wiring. The Pico's own regulator then makes 3.3 V from 5 V, and the radio no longer loads the console's 3.3 V rail.

## 0.6 Range (2026-11-08)

**Setup:**
- DOL-001 closed, in its usual place.
- RSSI of `PicoBoot-Measure-C2C4` read on the phone, from the side where the owner sits.
- Download test at 5 m.

| Board position | 1 m | 3 m | 5 m | Download at 5 m |
|---|---|---|---|---|
| A (the install position) | −29 dBm | −33 dBm | −44 dBm | 400–600 KB/s |
| B (antenna end away from metal) | −28 dBm | −32 dBm | −42 dBm | 400–600 KB/s |

**Findings:**

1. **Range isn't a problem** (risk R3).
   - −42 to −44 dBm at 5 m through the closed console is a strong signal; above about −60 dBm counts as excellent.
   - The shielding costs little, and the board's position hardly matters (1–2 dB).
2. **The download at 5 m is slower than on the bench** (400–600 against 800–950 KB/s), despite the strong signal.
   - Likely limits: lwIP's small send buffer (see 0.2) and possibly the radio on the 3.15 V rail (see 0.5).
   - Still enough for OTA: a 256 KB firmware in under a second, Swiss in about 3 s.

## 0.7 Stress test (2026-11-08)

**Setup:**
- Build `c031e24`, Swiss r2119 from flash.
- A 500 MB download (`/stream?mb=500`, about 10 minutes) to the phone, while a game loaded from the SD2SP2 and played through the HDMI adapter.
- Then 5 power cycles with about 5 s off.

**Results:**

| Check | Result |
|---|---|
| Game, SD loading, video, download | **No problems** seen during the run |
| Pico resets during the run | **None** (no BOR or WDOG entry; uptime continuous) |
| Boots (stress-run boot + 5 hot) | **6/6** good, margin 167.8–168.4 ms, CS 1544/1543 |
| Clocked CS count | 1544 on every boot: **equal to the raw count**, first one 6 µs after the first raw CS |
| RP2350 temperature reading | 22.6 °C (20.6 °C in 0.4; reads low because of the low rail, see 0.5) |
| CS at "PIO ready" | **LOW on every boot**, first seen high 41.7 ms later |

**Findings:**

1. **The console's rail carried the radio at full load, as far as anything visible goes.** No brown-outs, no Wi-Fi drops, and no console glitches with the SD2SP2 and HDMI adapter in use, despite the low 3.15 V average from 0.5.
2. **The clock-qualified counter agrees with the raw counter on hardware** on every good boot, so a CLK-qualified count of the 224 skipped pulses doesn't miss real transfers.
3. **CS isn't driven high for the first ~42 ms.**
   - CS reads low at "PIO ready" and goes high about 41.7 ms later, on every boot. The console's first IPL read follows at about 168 ms.
   - Boot 12's extra CS pulse came at 44.2 ms, about 2.5 ms after that first rise. The glitch belongs to the console's power-up.
   - Two explanations fit, and a CS pull-up separates them:
     - **(a) The console drives CS low until about 42 ms.** Then the glitch comes from the console, and a pull-up changes nothing.
     - **(b) Nobody drives CS until then, and the Pico's reset pull-down holds it low.** Then the glitch is the console letting go of the line for a moment, and a pull-up would keep CS high.
   - Build `d8ba49a` and later have a "CS pull-up" test switch for step 0.3b.
