# CS at start-up

Whether the IPL chip's CS is meant to be high when the console powers up, why the owner's two DOL-001s differ, and what that says about phase 0's false CS edge. Asked by the owner after release 1.1.1's console test. Nothing in the firmware changes.

**Short answer:** no source shows anything that holds CS high from power-on. On the dev console, CS is undriven until the console first drives it high, about 40 ms after "PIO ready". The second console's CS is already high 0.4 ms after the Pico starts, with either of two Picos, so it comes from the console; why isn't known.

The measurements are the owner's, from the boot logs of phases 0 and 1, the 1.1.1 series and release 1.1.1's console test. **[inf]** marks a conclusion rather than a sourced or measured fact.

## 1. What's measured

| | Dev console (`PicoBoot-C2C4`) | Second console (`PicoBoot-AE52`) |
|---|---|---|
| CS at "PIO ready", reset pull-down | **low** on every boot (phase 0; phase 1 from boot 56; the 1.1.1 series, 48 logged boots) | **high** on every boot ([cs-pull-up.md](cs-pull-up.md) §2, [release-1.1.1-results.md](release-1.1.1-results.md)) |
| CS at "PIO ready", pull-up | high | high |
| CS at "PIO ready", another Pico with new wires and a CubeSB board at U10 (2026-11-21) | — | **high** on every boot (boots 76–116, [main-console-tv.md](main-console-tv.md)) |
| The console first drives CS high | about 42 ms after "PIO ready" ([phase0-results.md](phase0-results.md) 0.7); the glitches came at 36–44 ms | not seen: CS is already high |
| The first IPL transfer | 158–168 ms after "PIO ready" | 157–160 ms; 155–157 ms with the other Pico |

All three Picos are RP2350 A2s, powered from the console's 3V3. Each samples CS once at "PIO ready", about 0.4 ms after `main()` starts.

## 2. The board

- **U10:** "MX RTC/IPL (8013108-M RTCN-DOL 1R6022A1)", with pin 1 clock, 5 CS, 6 serial in, 7 ground and 9 serial out (YAGCD, chapter 2, https://gc-forever.com/yagcd/chap2.html).
  - KunaiGC's board symbol for it names pin 5 `EXI-~{CS}`, pins 3 and 4 `+3.3V`, and pin 14 `+UBAT`, the clock battery (https://github.com/KunaiGC/KunaiGC).
  - No datasheet for U10 is public, so whether it has pulls of its own isn't known.
- **The CS net:** no traced schematic, service document or modchip guide found shows the parts on it.
  - PicoBoot's wiring photos show resistors near U10 (on the DOL-001, for example RA14, marked "332"), but nobody documents their nets.
- **Board revisions:** the DOL-001 came with boards DOL-CPU, -01, -10, -11, -20 and -30 (NTSC) and DOL-CPU(P)-01 and -10 (PAL) (https://consoleartisan.com/reference/gamecube/). No source names a change on the CS net.
  - The second console's board is DOL-CPU(P)-01, the first PAL revision (the owner, 2026-11-21). The dev console's isn't known yet.
- **The power boards:** the console's DC-DC board comes from three makers: Mitsumi (LSJB1091-1), Sharp (DUQ1230A) and TDK. Its electrolytic capacitors are a known problem "on Mitsumi LSJB1091-1 and Sharp DUQ1230A boards only"; "TDK boards are all polymer" (https://retrotechwiki.com/Nintendo_GameCube_Troubleshooting_Guide). The owner has seen at least two versions.
  - **The rails differ:** the second console's 3V3 at the Pico (pin 36 to GND) reads 3.38 V with the access point up at medium Tx power (the owner, 2026-11-21). The dev console's read 3.15–3.16 V with the access point up and 3.21 V with the radio off ([phase0-results.md](phase0-results.md) 0.5).
  - [inf] A different or younger power board in the second console would fit both its higher rail and its earlier drive of CS. That's not proof; the power boards' makers would tell more.
  - [inf] A different board, or ageing capacitors on one, could bring the rails up at a different pace, and with them the moment Flipper starts driving CS. No source measures this.
- **A bound from the dev console [inf]:** CS reads low there against the Pico's pull-down (36–113 kΩ). So a pull-up on that board's CS, if there is one, is weaker than about 112–353 kΩ.
  - A stronger one would put CS high, at 23–73 kΩ or less, or in the undefined region, where E9 lifts it high (§4).

## 3. The console at power-up

- **The 224 skipped transfers:** the boot ROM reads the IPL's first 0x700 bytes "in 8 byte blocks"; "the rest is transferred using large DMA blocks (1024 bytes)" (CCC 2004, "Gamecube Hacking", pp. 30–31, https://cdn.media.ccc.de/congress/2004/papers/208%20GameCube%20Hacking.pdf).
  - 0x700 ÷ 8 = 224, so PicoBoot's 224 skipped transfers are that first part, and the 225th is the first 1024-byte block [inf: the arithmetic].
- **Not found:** the state of Flipper's EXI pins while it's held in reset, the console's reset IC, and any power-up timing.
- **From the measurements [inf]:**
  - On the dev console, the console drives CS about 120 ms before its first IPL read.
  - The first read comes 157–168 ms after "PIO ready" on both consoles, so both Picos start at about the same point of their console's power-up.

## 4. RP2350-E9 can't make CS high on its own

The erratum (RP2350 datasheet, Appendix D.5.1):
- "When the pad is set as an input … and the voltage on the pad is within the undefined logic region, the leakage current exceeds the standard specified IIN leakage level … typically around 120μA. This leakage will hold the pad at around 2.2 V";
- "The pad pull-down (if enabled) is significantly weaker than the leakage current in this state and therefore isn't strong enough to pull the pad voltage low";
- "This doesn't affect the pull-down behaviour of the pads immediately following a PoR or RUN reset because the input enable field is initially clear";
- "If the pad is already a logic-0, re-enabling the input doesn't disturb the pull-down state".

**What that means here [inf]:**
- The pad's input stays off until `pio_gpio_init()` turns it on, about 0.3 ms before "PIO ready". Until then the pull-down works normally.
- A CS at 0 V at that moment stays low, as on the dev console.
- So on the second console, something already held CS high (or at an in-between level) when the input came on: the console driving it, or a resistor on the board. E9 can then keep it high, but not make it high.
- **Phase 1 fits this:** its first pull-down test switched from the pull-up to the pull-down, so CS passed through the undefined region with the input on, and E9 held it high on all 20 boots ([phase1-results.md](phase1-results.md), Part D).

**Correction:** [cs-pull-up.md](cs-pull-up.md) §2 listed E9 as a possible cause of the high CS on its own; it can only add to one of the others.

## 5. The false edge on the dev console

**Phase 0's explanation** was that the console's CS driver lets go of the line for a moment, and the pull-down pulls it low ([phase0-results.md](phase0-results.md) 0.3b, the phase 0 handoff, PicoBoot-DX's comment on its pull-up). It was marked as an inference.

**On an A2 chip that's unlikely [inf]:** a line released from high falls only into the undefined region, where E9's leakage beats the pull-down and holds it at about 2.2 V. That reads high, so a release makes no falling edge.

**The edge depends on how CS starts:**

| CS before the console drives it | Boots | Glitches |
|---|---|---|
| Low (the reset pull-down) | phase 0: 56; phase 1: 40; the 1.1.1 series: 48 logged | 2, 2, 1: **5 in 144** |
| High (the pull-up) | phase 0: 50; phase 1, part A: 20 | 0 |
| High (the pull-down, but E9 holding it after the pull-up) | phase 1, part D's first try: 20 | 0 |

- At 5 in 144, no glitch in 90 boots would happen by chance about 4% of the time [inf].
- A pulse that the console drives low would come whatever the line's starting level, and a weak pull-up couldn't stop it [inf].
- **So the most likely cause:** the console's first rise of CS isn't clean. The line goes up, falls back below the low threshold, then rises for good, as the console's CS driver comes up [inf]. Starting high, the same start doesn't cross the thresholds.
- **On RP2040 Picos,** which don't have E9, a release would read low, so phase 0's explanation could hold there [inf].

**What's measured still stands:** the edge comes as the console first drives CS, it has no clock, and D36's clock-qualified count ignores it (3 in 91 boots since phase 1, none missed). The fix doesn't depend on the mechanism.

## 6. Other reports

Upstream PicoBoot has console-dependent intermittent failures that were never explained:
- **#29**, "picoboot does not work with some power supplies": "picoboot sometimes does not work and the original gamecube menu appears", and "with another power supply it works correctly 100% of the time" (a DOL-001 with an SD Gecko) (https://github.com/webhdx/PicoBoot/issues/29).
- **#30**, "Flat Battery (revised)": 3 of 10 NTSC consoles didn't boot with a flat clock battery, and a good battery fixed them. The maintainer: "I'd be very surprised to learn it's caused by the battery" (https://github.com/webhdx/PicoBoot/issues/30).

No report ties a failure to the board revision or to CS's level at start-up. Upstream has no CS counter, so a glitch can't be told apart from other causes there.

## 7. Checks the owner could do

1. **Free:** remove the GC2SD and the HDMI adapter, or use another power brick, and read "CS at PIO ready" in the boot log.
2. **At the console's menu, with a multimeter:** the DC voltage on the Pico's CS pin. About 3.3 V means the console drives it high; about 2.2 V means it's undriven and E9 holds it.
3. **With the console unplugged and the Pico's CS wire lifted:** on both consoles, the resistance from U10 pin 5 (CS) to 3V3 and from pin 5 to GND, with pin 6 as a reference. Check that pin 5 reads 0 V first.
   - A steady reading of a few kΩ to a few hundred kΩ to 3V3, the same in both probe directions, on the second console only, means a resistor on that board.
   - Megohms or a reading that drifts, like pin 6, means none.
4. **The power boards and the dev console's board revision:** note which maker's power board each console has, and the dev console's DOL-CPU label, when it's open. If the dev console is a DOL-CPU(P)-10, the revision becomes a candidate. If they differ, swapping them settles the power board's part, through "CS at PIO ready" again.
5. **Swap the Picos** between the consoles. Whether CS high follows the console or the Pico settles the Pico's part. This is the most work.
   - **Partly done (2026-11-21):** another Pico, with new wires and a CubeSB board, reads CS high in the second console too (§1). So the high doesn't come from the Pico or the wiring. That Pico in the dev console would complete the swap.

## 8. Conclusions

1. **CS isn't meant to be high from the start,** as far as any source or measurement shows. On the dev console it's undriven until the console drives it.
2. **The second console holds CS high from at least 0.4 ms after the Pico starts,** and it follows the console: another Pico with new wiring reads it high too. The console drives it early, for example because its power board brings the rails up differently, or a resistor on its DOL-CPU(P)-01 board pulls it up. E9 alone can't do it. The checks in §7 would tell.
3. **1.1.1 doesn't depend on which:** CS keeps the reset pull-down, and the clock-qualified count ignores the false edge.
4. **The false edge needs CS to start low,** and on an A2 chip it's most likely a first rise that isn't clean, not a release. The notes that gave the release explanation point here.
