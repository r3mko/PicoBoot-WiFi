# Noise between payload blocks: glitches and stray clocks (1.2.1)

What still breaks 1.2.0's payload once its blocks are under way, from PicoBoot-DX's review of the block-waits handoff ([picoboot-dx-block-waits.md](../handoff/picoboot-dx-block-waits.md)), checked and extended in PicoBoot-WiFi's PIO simulator. For release 1.2.1's design questions. **[inf]** marks a conclusion rather than a measured or sourced fact. Every result here is the simulator's (`tests/pio_sim.py`); none is from a console.

**Status:** research, approved by the owner on 2026-11-24, for 1.2.1's design questions.

**Short answer:**
- **Two noise events together still break 1.2.0, with either method.** First something that sets IRQ 0 early:
  - a clockless CS glitch between payload transfers;
  - or a CS blip inside a block.

  Then a clock while CS is high, before a later block. D48 named this as "what's left" [inf]; it is now simulated.
- **One instruction in each output program closes most of it:** a wait for the next transfer's CS fall, after the wait for CS high (option B, §4). PicoBoot-DX's fix, in `on_transfer`, closes less (option A).
- **Still open with any option:** a stray clock right after a glitch, before the next transfer.
- **The shift itself was described wrongly:** later blocks come early, by one more bit each block, not "one bit late" (§2). The docs are corrected.

## 1. Where this comes from

PicoBoot-DX took the handoff's three waits in its commit `45d8805`, then changed `on_transfer` in `0a84825`. Both are on PicoBoot-DX's main, 2026-11-24, not yet on a console. Its session sent four findings:

| PicoBoot-DX's finding | Checked in PicoBoot-WiFi |
|---|---|
| 1. The shift goes the other way, and grows | **Confirmed** (§2) |
| 2. With the three waits, a glitch followed by a stray clock still shifts the data | **Confirmed, for both methods** (§3) |
| 3. CYW43's PIO: the SDK's `pio_claim_free_sm_and_add_program…()` searches from the highest PIO down | Nothing changes here. Wi-Fi's driver takes pio2 after the injection, as PicoBoot-WiFi's R4 review read it |
| 4. Padding the waits out with `mov y, y` hides the old bug at a 32 ns clock: the three extra cycles miss the block's last clock | PicoBoot-WiFi's negative tests remove the instructions instead (`without()` in `tests/test_length_injection.py`), so they keep 1.1.2's timing |

## 2. The shift: early, and growing

**The run:** 1.1.2's `clocked_output`, five payload blocks, a CS blip in the middle of the first.

**The result:** in the k-th block after the blip, the console reads bit i+k at position i. The shift is 0, 1, 2, 3, 4 bits.

**Why:**
- the next block takes the blip's IRQ and starts on the first block's leftover clock;
- so it comes one bit early;
- it also ends one clock early, which leaves one more clock over for the block after.

The docs said "every later block one bit late". They now say shifted, by one more bit each block. The conclusion stands either way: the payload is wrong from that block on.

## 3. The residual: an IRQ ahead, then a stray clock

**The mechanism:** after a clockless glitch between transfers, or a blip inside a block, `on_transfer` has set one IRQ 0 too many. The same goes for the length method's detector, whose loop after the first block is the same `wait 1 CS; wait 0 CS; irq`.
- **What 1.2.0's waits do:** they keep the output in step, one IRQ ahead. Each block still starts on its own command clocks.
- **What breaks it:** a clock while CS is high, before a later block, then counts as that block's first command clock.
- **Where such a clock comes from:** EXI channel 0's clock is shared with the console's other devices on it [inf]. On clean wiring, L4's captures counted the same 21,503 BS1 edges on every boot, BS1's own clocks, so none came during BS1 there [inf]. Between payload blocks nothing has been captured.

## 4. The options

- **A, PicoBoot-DX's change** (`0a84825`):
  - **The standard method:** `on_transfer`'s wrap loop gets `wait 1 gpio CLK` after its `irq set 0`. `inject:` moves onto that `irq`, so the first IRQ gets the wait too. Still 10 instructions.
  - **The length method:** the detector's loop gets the same wait after its `irq prev set 0`. That's 15 instructions, and pio1 holds 25 of 32 with the counters.
  - **The effect:** a clockless glitch then merges with the next transfer instead of setting a second IRQ.
- **B, the output waits for CS low:** `wait 0 gpio CS` after the wait for CS high, in both output programs.
  - **The cost:** pio0 holds 30 of 32 with the standard method, and 29 with the length method.
  - **The effect:** a block's command clocks are counted only after the next transfer's CS fall, however many IRQs are pending. So a clock while CS is high between blocks can't count.
- **A+B:** both.

**The cases,** 3 payload blocks. "Wrong" means the payload arrives wrong or the output doesn't end at `disable_chip`:

| Case | 1.2.0 | A | B | A+B |
|---|---|---|---|---|
| Clean; a glitch, a blip or a stray clock alone | ok | ok | ok | ok |
| A glitch after block 0, a stray clock after block 1 | wrong, both methods | ok | ok | ok |
| A glitch before the 225th transfer, a stray clock after block 0 | wrong, standard; the length method is ok | ok | ok | ok |
| A blip in block 0, a stray clock after block 1 | wrong, both | **wrong** | ok | ok |
| A glitch after block 0, a stray clock right after it, before block 1 | wrong, both | wrong | wrong | wrong |

**No regressions with B or A+B** in what 1.2.0's tests cover:
- the 272 blips at a block's end, for both methods;
- CS low at the start, for the standard method;
- the measured EXI timing at full size, 3 seeds, for both methods.

**Not checked yet:** the whole test suite with B in the programs. That is the release's first step.

## 5. What's left with every option

**A stray clock right after a glitch:** CS falls (the glitch), rises, then a clock comes before the next transfer. To B it looks like a transfer that started. To A it is the clock that ends the glitch's wait.

**A further idea, not simulated:** count a command clock only while CS is low. That would be a `jmp pin` check in the output's command-clock loop, as `on_transfer` checks its skipped transfers.
- It would need JMP_PIN on CS for the output state machine.
- After a stray clock it would have to go back to the CS waits without waiting for a "last clock" again, so it means more than one instruction [inf].
- It's for the design questions, not a recommendation.

## 6. What a 1.2.1 would take

- **The PIO change:** B, or A, or both. B touches only the two output programs, which stay alike from `start` on.
- **Tests:** this note's cases go into the suite, for both methods. That includes the clockless glitch between payload transfers from the handoff, and the shift direction (§2) as a negative test.
- **Docs:** D48's "what's left", the README's lines on the waits if any, and the release notes.
- **A console test:** both consoles, as 1.2.0's R5. Clean wiring can show only that nothing broke.
- **PicoBoot-DX:** B covers more than its `0a84825`, which is worth a note back. Its `on_transfer` is PicoBoot-WiFi's.
