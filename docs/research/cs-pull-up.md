# D36's CS pull-up on a second console

Why PicoBoot-WiFi, and PicoBoot-DX from v0.11, don't boot the owner's second DOL-001, and what that means for D36's pull-up. For release 1.1.1; the owner chooses the scope from this note.

**Later finding (2026-11-21):** the second console's wires (loose wires straight to U10) turned out to be marginal. At its TV they picked up noise even with the pull-down, until new, shorter wires fixed it ([main-console-tv.md](main-console-tv.md)). Every pull-up result below was measured on those wires. Whether the pull-up harms a console with good wiring isn't known; on the dev console's flex cable it never did. The pull-up wasn't tried on the second console's new wiring.

**Setup:**
- **The second console:** the owner's main DOL-001, with a GC2SD in a memory-card slot and an HDMI adapter.
  - It used to have a Pico with PicoBoot-DX, which worked.
  - It now has a new Pico 2 W (RP2350 A2, `PicoBoot-AE52`), on the old wires, on 3V3.
- **The dev console:** the DOL-001 from phases 0 to 5 and the releases (`PicoBoot-C2C4`, RP2350 A2).
- **Test builds:** `1.1.1-dev` builds of 1.1.0 with the test page, which has a switch for the CS pull. The switch is the "CS pull-down test" flag (D36), which leaves the pad as reset leaves it, as PicoBoot does. Built locally, not committed:
  - `+9a10a77`: as released, 8 mA;
  - `+606476d`: the data pin at 12 mA;
  - `+1733d98`: PicoBoot-DX v0.10's skip count, every CS edge.

The counts and the boot results come from the owner's boot logs and reports of 2026-11-20. **[inf]** marks a conclusion rather than a measured fact.

## 1. The symptom, and the noise that hid the cause

- **What the second console did:** it showed its own IPL menu on every boot. A payload never started, neither the built-in gekkoboot nor Swiss as an IPL build in payload 2.
- **What the Pico saw:** every injection was complete, with the dev console's timing to the microsecond. But the CS counters read 11,492–59,154 transfers for the built-in (285 expected) and up to 919,302 for Swiss (1544 expected). Raw and clocked nearly agreed.

| Boots | Change | Payload | CS mode | `cs_raw` (expected) |
|---|---|---|---|---|
| 1–10, 12 | 1.1.0 and `+9a10a77`, 8 mA | the built-in | pull-up | 11,492–59,154 (285) |
| 13, 19–23 | `+9a10a77`, 8 mA | Swiss r2119 | pull-up | 247,927–250,138 (1544) |
| 14–18 | `+9a10a77`, 8 mA | Swiss r2119 | pull-down | 56,146–57,284 (1544) |
| 24 | the GP6 wire shortened | Swiss r2119 | pull-up | 260,735 (1544) |
| 26–29 | `+606476d`, **12 mA** | Swiss r2119 | pull-up | 447,340–919,302 (1544) |
| 30–32 | `+606476d`, **12 mA** | Swiss r2119 | pull-down | 339,265–340,449 (1544) |

- **The noise came from the Pico's own data output:** a stronger drive gave two to six times more CS edges, and shortening the data wire changed nothing. The owner then shortened the **ground** wire, and the counts dropped to the expected 285 to 289.
- **So it was ground bounce [inf]:** the data line's current through a long ground wire moved the Pico's ground, and CS crossed its threshold during transfers.
  - **Most of that current is likely the fight with U10 [inf]:** the Pico overpowers U10's own output on the data line, so current flows between the two drivers whenever their bits differ, not only at the edges. A stronger drive makes that current bigger, which fits the two to six times more edges at 12 mA ([gc-docs.md](gc-docs.md) §3, point 3).
- **The noise didn't move the injection's start,** because the Pico drives the data line only after the 224 skipped transfers.
- **But the long ground wire was a cause of its own:** with it, the console didn't boot even with the pull-down (boots 14–18, 30–32). After it was shortened, the pull-down boots worked (§2). A bouncing ground probably let U10 win the data line over the Pico [inf].
  - **A simpler cause, found in D47's step L1:** in the PIO simulator, an extra that splits a payload block makes every later block one bit late ([length-injection.md](length-injection.md) §2). The long ground wire's tens of thousands of extras would do that on every boot.
- **12 mA**, as upstream PicoBoot drives its single data pin since `e9f420b` (2024), didn't help.

## 2. After the ground fix: the pull-up

The owner bisected PicoBoot-DX on the same Pico. v0.9 and v0.10 boot, v0.11 and v0.12 don't. v0.11 added both of D36's fixes: the CS pull-up (`0158695`) and the clock-qualified skip count (`0be9e59`).

Two test builds on the second console separate the two:

| Build | CS mode | Boots | `cs_raw` (285 expected) | The console booted |
|---|---|---|---|---|
| `+1733d98`, every CS edge counts | pull-up | 1–7 | 289, 285, 286, 285, 285, 285, 286 | only on the 285s |
| `+1733d98` | pull-down | 8–14 | 285 every time | 7 of 7 |
| `+9a10a77`, clock-qualified count | pull-down | 16–22 | 285 every time | 7 of 7 |
| `+9a10a77` | pull-up | 23–28 | 286, 287, 285, 285, 286, 286 | 2 of 6, the 285s |

- **With the pull-up, about half the boots get one to four extra CS transfers,** and every one of those boots fails. The extra transfers are clocked (raw equals clocked), so the clock-qualified count counts them too, and the payload goes out early [inf: where the extra transfers fall wasn't measured].
- **With the pull-down,** both counts are exact, and the console boots 14 of 14.
- **CS reads high at "PIO ready" on every boot here, in both modes.** On this console something holds CS high from the start: the console or a resistor on its board [inf]. RP2350-E9 can keep CS high but not make it high on its own ([cs-at-start-up.md](cs-at-start-up.md) §4). That differs from the dev console (§3).
- **Not known:** how the pull-up produces clocked extra edges on this console. Only the effect is measured, on its wiring of the time (see the later finding at the top) [inf: the pull-up may need marginal wiring to do harm].

## 3. The dev console without the pull-up

The pull-up was added for the dev console (phase 0). There, the reset pull-down holds the undriven CS low until the console first drives it, about 36–44 ms after "PIO ready". A clockless edge at that moment shifted PicoBoot-DX v0.10's count, and the console blue-screened on 2 of 56 cold boots.

- **Phase 1 already proved the clock check without the pull-up:** in the test mode, 2 such glitches in 40 boots, 36–37 ms after "PIO ready". Both were ignored, and Swiss started (D36).
- **The series for this note** used `+9a10a77` in pull-down mode, boots 164–214: 51 power-ons, every one of them starting the payload (the owner). Boots 190–192 fell out of the boot log between its two reads; 48 are logged:
  - every logged boot has the exact count: 1544 for Swiss, 942 for cubiboot with the D43 stub (boot 164);
  - CS reads low at "PIO ready" on every boot;
  - **boot 214 had the phase 0 glitch:** its first CS edge came 36.5 ms after "PIO ready", against 160.8 ms for the first real transfer. It counted raw 1545 against clocked 1544, so the clock check skipped it, and Swiss started;
  - margins 159.2–163.7 ms.

## 4. Conclusions for release 1.1.1

1. **Drop D36's pull-up and keep its clock-qualified count.** On the dev console the clock check does the pull-up's job: 3 glitches caught in 91 boots over phases 1 and 1.1.1, none missed. On the second console, on its wiring of the time, the pull-up is the cause of the failures.
2. **No count can protect against clocked extra edges.** Leaving CS as reset leaves it is the only measured setting that works on both consoles, and it also worked on the second console's marginal wiring (35 of 35 boots at the bench).
3. **The test page's switch** could stay, inverted, as a way to turn the pull-up on for a diagnosis.
4. **The wiring matters:** a long ground wire made the data line's switching show up as CS edges. The README's install notes could ask for a short ground wire near U10. The boot log's CS counts show the problem: far more transfers than expected.
5. **PicoBoot-DX has had the same pull-up since v0.11,** with the same effect on the second console. A handoff note, like phase 0's, would bring the finding back.
6. **Not changed:** the 8 mA drive (12 mA didn't help); the data and clock wires.
