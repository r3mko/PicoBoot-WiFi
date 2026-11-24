# Handoff to PicoBoot-DX: wait for each payload block to end

**From:** PicoBoot-WiFi ([r3mko/PicoBoot-WiFi](https://github.com/r3mko/PicoBoot-WiFi)), release 1.2.0, 2026-11-24.
**For:** whoever works on PicoBoot-DX ([r3mko/PicoBoot-DX](https://github.com/r3mko/PicoBoot-DX), v0.13). It follows [picoboot-dx-cs-glitch.md](picoboot-dx-cs-glitch.md) and [picoboot-dx-cs-pull-up.md](picoboot-dx-cs-pull-up.md).

**In short:**
- **The weakness:** `clocked_output` reaches `wait 1 irq 0` while one clock of the payload block is still to come. An IRQ 0 that is set early lets that clock count as the next block's first command clock, and every later block is then one bit late. Two kinds of noise set it early:
  - a CS blip inside a payload block;
  - a clockless CS glitch between payload transfers.
- **PicoBoot-DX knows the second already:** the "Not covered" comment on `on_transfer` says one glitch between payload transfers "shifts a later transfer by one bit". Restoring the wrap-loop CLK wait that `01acbb1` removed is parked for it.
- **The change:** three instructions in `clocked_output`, after `set x, 31`. It waits for the block's last clock, then for CS high, then for the IRQ. pio0 goes from 26 to 29 of 32 instructions.
- **Status:** in PicoBoot-WiFi 1.2.0, released 2026-11-24.
  - **Shown so far:** simulated, code-reviewed, and booted on two DOL-001s with Pico 2 Ws (RP2350).
  - **Not yet:** an RP2040.
  - **PicoBoot-DX:** nothing has changed. Whether and how to take it is up to the owner.

## 1. The mechanism

**Where one clock is left over:** a payload block is 32 command clocks and 8192 data clocks.
- `clocked_output` drives bit 0 after the 32nd command clock, and each later bit after the next rising edge.
- So the block's last bit goes out after its second-to-last clock.
- `jmp !osre start` then goes back to `start`, which waits for IRQ 0 while the block's last clock is still to come.

**Without noise,** IRQ 0 comes only when `on_transfer` sees the next transfer's CS fall, after that clock. With noise, it comes early:

| Noise | What `on_transfer`'s wrap loop does | Result in PicoBoot-DX v0.13 |
|---|---|---|
| A CS-high blip inside a payload block | Sees the blip end as a CS fall and sets IRQ 0 | The output takes the block's last clock as the next block's first command clock: every later block one bit late |
| A clockless CS-low glitch between payload transfers | Sets IRQ 0 at the glitch, then again at the next transfer's real CS fall | That second IRQ is left set. At the end of the next block it passes at once, and the same shift follows |

**What each new wait does:**
- **The wait for CS high:** the block is over only when CS rises. On its own it closes both cases, except for one window. A blip that ends just before the block's last clock passes it, because CS is high during the blip.
- **The wait for the block's last clock, before it:** closes that window. PicoBoot-WiFi's code review found the window (plan step R4).

## 2. The change

**`src/picoboot-dx.pio`, `clocked_output`:**

```
start:
    set x, 31                   ; x - 32 bits we need to skip (address)
    wait 0 gpio CLK_PIN         ; The block's last clock: its last bit went out at the clock before,
    wait 1 gpio CLK_PIN         ; so a CS blip before this clock can't pass the wait below.
                                ; Before the first block: any clock of BS1
    wait 1 gpio CS_PIN          ; The block is over only when CS rises: noise that splits it leaves
                                ; IRQ 0 set early, and without this wait its last clock would count
                                ; as the next block's first command clock
    wait 1 irq 0                ; Hold until SM0 tells us to start injecting
```

**The comments:** the second half of `on_transfer`'s "Not covered" comment goes. Its first half stays: a glitch right before the 225th transfer.

**The first block:** `clocked_output` starts at `start` too, so it waits for a BS1 clock and a CS rise before the first IRQ. BS1 gives both long before transfer 225.

**What it costs:**
- **pio0:** 29 of 32 instructions (`on_transfer` 10, `clocked_output` 19).
- **The W builds:** `led_init()` calls `cyw43_arch_init()` after the injection's programs are in pio0 (`src/picoboot-dx.c:167`). So CYW43's 6-instruction SPI program has to find room in another PIO. The SDK's driver looks for a PIO with room [inf: PicoBoot-WiFi's R4 review read it searching from the highest PIO down]. **Check that a W build's LED still comes on:** `led_init()` panics if `cyw43_arch_init()` fails.

**The parked `01acbb1` restore** isn't needed for the glitch between payload transfers any more [simulated, §3]. One difference is left:
- **With the restore,** a clockless glitch sets no IRQ at all.
- **With the waits,** it sets one early. The output then stays one IRQ ahead but in step, as long as CLK doesn't run while CS is high between blocks [inf].

## 3. The evidence

**Simulator:** PicoBoot-WiFi's [`tests/test_length_injection.py`](https://github.com/r3mko/PicoBoot-WiFi/blob/main/tests/test_length_injection.py). Its standard-method cases run `on_transfer` and `clocked_output` as PicoBoot-DX does.

| Case | 1.1.2's `clocked_output`, which is PicoBoot-DX v0.13's | The CS wait alone | Both waits (1.2.0) |
|---|---|---|---|
| A CS blip in the middle of a payload block, phases 2–7 (`test_noise_during_the_payload`) | loses a bit | in step | in step |
| Blips of 1–4 cycles at a block's last two clocks, with 32, 40 and 64 ns clocks: 272 cases (`test_a_blip_at_a_blocks_end`) | loses a bit in 70 | loses a bit in 32 | in step in all |
| The same at the measured EXI timing, 40 seeds (`MeasuredConsole`) | loses a bit in all 40 | loses a bit in 8 | in step in all |
| A clockless CS glitch between payload transfers, 24 cases. Run for this handoff, not in the suite yet, as are 1.1.2's numbers in the two rows above | loses a bit in 12, every one before the last block | in step | in step |

**The clean cases** (a full BS1, two 1 KB blocks, the measured timing) deliver the payload exactly. They end at `disable_chip`, with the data line taken over once and released once.

**Code review** (PicoBoot-WiFi plan step R4, before the wait for the last clock):
- 1.2.0's standard method was 1.1.2's, instruction for instruction, plus the wait for CS high.
- That wait can't stall the first block, including when CS reads low at the start. The suite's cases with CS low at the start pass with both waits.
- The review found the window that the wait for the last clock then closed.

**On hardware** ([release-1.2.0-results.md](https://github.com/r3mko/PicoBoot-WiFi/blob/main/docs/research/release-1.2.0-results.md)):
- **The setup:** Pico 2 Ws (RP2350) at 250 MHz, as PicoBoot-DX boosts, on both DOL-001s.
- **The boots:** 4 with the standard method and 6 with 1.2.0's length-based one, which has the same waits.
- **The result:** every payload started, with exact CS counts and the margins of before.
- **The limit:** clean wiring only, so the noise cases are the simulator's.

**Not shown:** an RP2040 on hardware. The waits are plain `wait gpio` instructions, and the timing is the same at 250 MHz [inf].

## 4. Tests for PicoBoot-DX

PicoBoot-DX's simulator is where PicoBoot-WiFi's started. It has `Console(half_period=…, gap=…)`, `add_pulse` and `simulate(patch=…)`, so the cases port with little:
- **The cases:**
  - a CS blip in the middle of a payload transfer;
  - a blip at a transfer's last two clocks, at several clock speeds;
  - a clockless CS glitch between payload transfers, before a transfer that isn't the last.
- **What they should show:** each delivers the payload with the new program. A `NegativeTest` that patches the three waits out shows it failing, as `without_hold` does for the end of the payload. `delivery_problems` already checks the end: `disable_chip` and the direction changes.
- **PicoBoot-WiFi's helpers:** `blip`, `clock_time` and `MeasuredConsole`, in `test_length_injection.py`. `clock_time` assumes the Console's timing: CS falls, then `gap`, then the clocks. Check that against PicoBoot-DX's Console before copying.

## 5. Not for PicoBoot-DX now: length-based injection

1.2.0's other change, injection by the first block's length (design D48), doesn't port simply:
- **RP2040:**
  - it has no `irq prev` from one PIO block to another, and no `IN_COUNT` to read CLK alone with `mov y, pins`;
  - the detector and the output in one block would need 14 + 28 = 42 instructions, over the limit of 32.
- **RP2350:** the programs would run as they are. But PicoBoot-DX has no page or settings to choose the method, so it would mean separate UF2s.
- **Its benefit is untested on noisy wiring,** and one CLK glitch during BS1 starts its payload early.

## 6. Also learned: the measured EXI timing

From [length-injection-results.md](https://github.com/r3mko/PicoBoot-WiFi/blob/main/docs/research/length-injection-results.md), a 4 ns capture on both consoles:
- **BS1:** 27.00 MHz (37.04 ns a period), 224 fetches of 96 clocks without a pause.
- **The first block:**
  - CS falls about 312–316 ns before its first clock;
  - CLK pauses 660 ns after the 32 command clocks;
  - one data period in 64 is stretched to about 50 ns.
- **BS1 ignores the first block's first 66 bits:** shown on hardware by the length-based boots, in which U10, not the payload, sent them.
