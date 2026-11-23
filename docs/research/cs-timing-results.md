# CS timing results (D44)

The console series of the diagnostic build (the plan's step T3), run by the owner on 2026-11-22 on both consoles.

**Status:** T3 done; T4 approved by the owner. T5, the decision on what follows, comes next.

**Build:** `1.1.2-dev+52d2603`, the build after the code review (T2), installed over Wi-Fi on both consoles:

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.1.2-dev-update.bin` | `5ff85a4af9f842dfad4e7128644b2f74dda0771d86aa59231d8b4c84359bc25f` |
| `picoboot-wifi-1.1.2-dev.uf2` | `1f0bda2fecec53e987c6475076fa0d72e1820d89236730d30f1b0a98426e95bf`, from the same build; not installed |

**Setup:**
- **The dev console:** `PicoBoot-C2C4` on Helder Game Tech's flex cable, 3V3. The built-in gekkoboot, and Swiss r2174 in payload 1 (885 transfers).
- **The main console:** a new Pico 2 W (RP2350 A2) on the CubeSB board with 24 AWG wires, 3V3, at its TV ([main-console-tv.md](main-console-tv.md)). The built-in, and Swiss r2174 in payload 2.

Each console's boot log started afresh with the test build (D44), so boot numbers start at 1 on both. The owner downloaded the boot logs and the raw records of boots 2 and 7 (dev console), and 2, 7, 16, 17, 18 and 24 (main console).

Times are as the recorder reads them: each one 8–20 ns short (simulator, D44). **[inf]** marks a conclusion rather than a measured fact.

## 1. The boots

| Console | Boots | Payload | Result |
|---|---|---|---|
| Dev | 2–6 | the built-in | 5 of 5 complete; boot 2 with a glitch (raw 286, clocked 285), the others 285 exact |
| Dev | 7–11 | Swiss r2174 | 5 of 5 complete, 1110 exact |
| Main | 7–18 | the built-in | 12 of 12 complete, 285 exact. 16–18 were quick power cycles |
| Main | 2–6, 19–24 | Swiss r2174 | 11 of 11 complete, 1110 exact |

- **Every one of the 33 power-ons started its payload** (the owner).
- **CS falls with CLK high: 0,** on every boot of both consoles.
- **The quick power cycles (main, 16–18) didn't stall.** Their transfers 225 and 226 are full 1 KB blocks, like every other boot's. The stalls before ([main-console-tv.md](main-console-tv.md) §4) didn't come back.

## 2. The timing: the same on both consoles

| | As recorded | What it is |
|---|---|---|
| A BS1 transfer, CS low | 3.656–3.664 µs | 32 address and 64 data clocks |
| Gaps in BS1 | mostly 136–152 ns | the next 8-byte fetch |
| Longer gaps in BS1 | before transfers 49 (208 ns), 53 (93.26 µs), 57 (232 ns), 61 (384 ns), 65 (2.67 µs), 69 (5.02 µs), 73 (280 ns) | every 4th transfer: 32 bytes, a CPU cache line. The CPU is running BS1's code [inf] |
| The gap before transfer 225 | 192 ns | BS1 hands over to the first 1 KB block: the shortest gap of all |
| A 1 KB block, CS low | 307.36–307.37 µs | 32 address and 8192 data clocks |
| Gaps between blocks | 192–336 ns, mostly 320–328 ns | |

- **Identical to the nanosecond on both consoles,** with different wiring and different Picos, down to the pattern of longer gaps. The boot ROM and BS1 decide this timing, not the console's hardware or the wiring [inf].
- **The EXI clock:** fitting CS-low time against clocks for the two kinds of transfer gives 26.76 MHz, plus about 75 ns of CS low outside the clocks [inf: assuming 96 and 8224 clocks]. That's the "27MHz SPI-like bus" of the CCC 2004 talk.
  - *Measured in D47's step L4* ([length-injection-results.md](length-injection-results.md) §4): BS1's clock is 27.00 MHz. The 1 KB blocks pause 660 ns after their command and stretch one clock in 64, so they average 26.86 MHz: the fit measured that average. CS falls 72–76 ns before a BS1 fetch's first clock and rises 64–68 ns after its last.
- **The earlier estimate was wrong.** [cs-timing-diagnostics.md](cs-timing-diagnostics.md) §2 inferred a 32 MHz clock and gaps of about 1.3 µs in BS1. The fit of the injection times was right (BS1 0.96 ms, a block 307.7 µs), but its split into transfers and gaps wasn't: BS1's 0.96 ms includes the 93 µs pause, and a block's 307.7 µs is nearly all CS low.

## 3. The start

| | Dev console | Main console |
|---|---|---|
| CS at "PIO ready" | low on every boot | high on every boot |
| CS undriven at first, then | 37.3–38.4 ms, then driven high | — |
| Then to the first transfer | 125.99–126.45 ms | 154.64–155.86 ms after "PIO ready" |
| Margin to the first transfer | 163.33–164.67 ms | 154.64–155.86 ms |

**The dev console's glitch (boot 2):**
- **What the counters saw:** raw 286 against clocked 285. The raw counter's first edge came at 38.363 ms after "PIO ready", which is where the undriven start ended: the console's first drive of CS.
- **What the recorder saw:** no separate CS-low period, so the dip merged into that first rise. CS fell back and rose again within about 20 ns of rising [inf: the recorder's merge window].
- **What that shows:** the console's first rise of CS isn't clean, which [cs-at-start-up.md](cs-at-start-up.md) §5 inferred. It's not a release, and not a later pulse.
- **It also shows in the injection time:** 145.6 ms instead of 19.4 ms, because `injection_us` starts at the first raw edge.

## 4. What the summary got wrong

- **`short_gaps` and `first_short_gap` are useless as built.** Their 500 ns threshold is above every real gap: they count all of them (280 for the built-in, 1105 for Swiss) and name transfer 2.
- **The shortest gaps (`bs1_gap_min_ns`, `payload_gap_min_ns`) work:** 136 ns and 192–200 ns on every boot of both consoles. A blip would show as a shorter one.

## 5. What it means for the next step (T5)

- **A minimum CS-high gap for D36's skip count:**
  - real gaps are 136 ns or more as recorded (about 150 ns actual);
  - the BS1-to-block hand-over is at 192 ns;
  - so a filter would have to ignore only CS-high pulses shorter than about 100 ns.
  - How short noise blips are is still unknown: these clean setups had none [inf].
  - The room is narrow, and a filter would only help against short blips.
- **The fourth wire** (injecting by address) doesn't depend on any of this, and stays the robust option.
- **A wiring-quality number for the releases:** the shortest CS-high gap of each boot was exactly 136 ns on both consoles. Noise on CS would show as a shorter one, so it would make a simple check on the Diagnostics tab. The 500 ns threshold would have to go.
- **D36's known limit,** a false edge while CLK reads high: CLK was low at every CS fall here, including boot 2's glitch.
- **The margins:** the test build zeroes about 1.7 KB less RAM than a release, so they may read about 0.2 ms high [inf].
