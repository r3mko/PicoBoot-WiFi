# Handoff to PicoBoot-DX: the CS glitch behind the occasional blue screens

**From:** PicoBoot-WiFi (`~/GitHub/PicoBoot-WiFi`), phase 0 hardware checks, 2026-11-08.
**For:** whoever works on PicoBoot-DX (`~/GitHub/PicoBoot-DX`, v0.10). The owner asked for these fixes to be brought back to PicoBoot-DX.

**In short:**
- **The failure:** PicoBoot's occasional blue screens at a cold boot come from **one false falling edge on CS**, about 42–44 ms after power-on, when the console first drives CS.
- **Why it breaks the boot:** `on_transfer` counts that edge as one of the 224 skipped transfers, so the payload goes out one transfer early and the console crashes.
- **Two independent fixes:**
  1. Pull CS **up** instead of leaving the pads' reset pull-down.
  2. In `on_transfer`'s skip loop, count a skipped transfer only once CLK clocks in it.
- **Status:** both were measured or simulated in PicoBoot-WiFi; details below. Neither is in PicoBoot-DX yet. It's up to the owner whether and how to take them.

## 1. The evidence

**Setup:**
- **Board:** one Pico 2 W, fed through 3V3 (the usual wiring).
- **Console:** a DOL-001 with an SD2SP2 and an HDMI adapter.
- **Firmware:** a measurement build that is **PicoBoot-DX v0.10 with the injection unchanged** (the `.pio` file is byte-identical, checked by SHA-256), plus passive counters in `pio1` and a boot log read over Wi-Fi.
- **Source:** `~/GitHub/PicoBoot-WiFi/hwtest/dx-measure`. Full results: `~/GitHub/PicoBoot-WiFi/docs/research/phase0-results.md`.

| Measurement | Result |
|---|---|
| CS level when the PIO starts (0.3 ms after `main()`) | **Low** on every boot, until the console first drives it high **about 41–42 ms later** |
| The console's first real IPL read | About **166 ms** after the PIO starts (164–168 ms over 100+ boots) |
| Failed boots | 2 cold boots out of 56 with PicoBoot's pad settings. **Both blue-screened**, and both logged **one extra CS falling edge**, at 44.2 ms and 42.0 ms after the PIO started (as the console first drove CS), with no clocks |
| Second counter (counts a CS pulse only once CLK clocks in it) | Ignored the false edge, and agreed with the raw CS count on every good boot (1544 = 1544 for Swiss r2119) |
| CS with the pad **pull-up** on instead | Reads **high** from the start. **0 failures in 50 cold boots** |

**Also reported by the owner:** the same occasional blue screens on this console with PicoBoot-DX v0.9 and earlier, and with upstream PicoBoot.

## 2. The mechanism

1. **CS is active-low and undriven for ~42 ms.** The console leaves the IPL chip's CS line undriven for about 42 ms after power-on.
2. **PicoBoot never sets pad pulls.** `pio_gpio_init()` calls `gpio_set_function()`, which "doesn't affect e.g. pullup/pulldown" (SDK `gpio.c`). So CS keeps the pad's **reset pull-down** (`PADS_BANK0_GPIO0_PDE_RESET = 1`, on RP2040 and RP2350 alike), and reads low, "selected", until the console drives it.
3. **The false edge.** When the console's CS driver comes up, it can let go of the line for a moment. The pull-down then pulls CS low, and the PIO sees a falling edge without a transfer [inference from the timing; the pull-up result supports it].
4. **The shifted payload.** `on_transfer` counts CS falling edges without looking at CLK:
   ```
   start:
       wait 1 gpio CS_PIN
       wait 0 gpio CS_PIN
       jmp x-- start               ; X = 224
       irq set 0
   ```
   With one false edge, `irq set 0` fires on the 224th real transfer instead of the 225th, so the whole payload is sent one transfer early.
5. **RP2350 A2 boards have it worse:** erratum **RP2350-E9** (datasheet Appendix D.5.1, fixed in A3) can hold a floating input with input-enable at about 2.2 V, which the pull-down can't overcome. "The pad pull-up still works."

**The simulator reproduces it:** `~/GitHub/PicoBoot-WiFi/hwtest/dx-measure/tests/test_measure.py`, `GlitchTest`. One CS low pulse without clocks before the IPL read, and PicoBoot-DX's programs deliver a shifted payload.

## 3. Fix 1: pull CS up

**`src/pio.c`, `on_transfer_program_init()`**, right after `pio_gpio_init(pio, cs_pin);`:

```c
    // Pull CS up: the console leaves CS undriven for ~42 ms after power-on, and the pad's reset
    // pull-down would hold it low ("selected"), so a brief release by the console reads as a CS pulse
    gpio_pull_up(cs_pin);
```

**Notes:**
- **When it runs:** before the state machines start, as one register write. In the measurement build (`pico2_w`, running from flash) it cost about 17 µs before "PIO ready", against a 166 ms margin.
- **Which boards:** all four. The RP2040 has the same reset pull-down.
- **Other pins:** leave CLK and DATA as they are. CLK idles low anyway, and DATA is only driven during the injection.
- **Side effect, a good one:** the real IPL chip (U10) stays deselected while the console powers up.

## 4. Fix 2: count a skipped transfer only once it clocks

**`src/picoboot-dx.pio`, `on_transfer`.** The jump of the skip loop goes through a new `skipped` block that waits for CLK before the next CS edge can count. The 225th transfer's path is unchanged.

```diff
 start:
     wait 1 gpio CS_PIN          ; Wait for CS to go HIGH
     wait 0 gpio CS_PIN          ; Wait for CS to go LOW
-    jmp x-- start               ; Skip 224 CS pulses (X = 224), continue on the 225th
+    jmp x-- skipped             ; Skip 224 CS pulses (X = 224), continue on the 225th
     irq set 0                   ; Set IRQ flag to start injecting payload
 
 .wrap_target
     wait 1 gpio CS_PIN          ; Wait for CS to go HIGH
     wait 0 gpio CS_PIN          ; Wait for CS to go LOW
     irq set 0                   ; Let other SM inject the payload
 .wrap
+
+skipped:
+    wait 1 gpio CLK_PIN         ; A skipped transfer only counts once it clocks: a CS glitch
+    jmp start                   ; without clocks merges with the next real transfer
```

**Why it works:**
- **A false edge** decrements X once. Then `wait 1 gpio CLK_PIN` holds until the next *real* transfer clocks, and `jmp start` waits for that transfer's CS to go high. The false edge and that real transfer together count once, so the 225th real transfer still raises the IRQ.
- **Without a glitch,** each skipped transfer decrements X at its CS edge and then passes its first clock. That's the same count as now.
- **The 225th transfer** (X = 0) falls through to `irq set 0` without waiting for CLK, so `clocked_output` still sees every address clock. `pio_prepare_transfer(..., 224, pio_x)` stays as it is.

**Assembled:** with `pioasm` (SDK 2.3.1), `on_transfer` becomes 9 instructions instead of 7. Check that the W builds' CYW43 PIO program still fits next to it; if not, it moves to another PIO block.

**Verified in PicoBoot-DX's own simulator** (`tests/pio_sim.py`, unchanged; one-off script in PicoBoot-WiFi):

| Case | v0.10 | With the fix |
|---|---|---|
| 3 transfers, no glitch | ok | ok, **bit-identical** to v0.10 |
| Built-in gekkoboot (60 transfers), no glitch | ok | ok, **bit-identical** to v0.10 |
| Glitch (CS low 2 cycles, no clocks) before the first transfer | broken ("data line driven during transfer 223") | ok |
| Glitch between skipped transfers 0 and 1 | broken | ok |
| Glitch between skipped transfers 100 and 101 | broken | ok |

**Known limit:** a glitch between the 224th and 225th transfer would still start the injection one transfer early. That's the X = 0 path, which deliberately doesn't wait for CLK. The glitches seen came about 120 ms before the console starts reading the IPL.

## 5. Tests to add in PicoBoot-DX

**In `tests/test_pio_programs.py`:**
1. **A glitch option for `simulate()`:** add a CS low pulse without clocks to the console, either before the first transfer or a few cycles after a transfer's CS rise.
   ```python
   def add_cs_glitch(console, start, width=2):
       """CS low for `width` cycles without clocks, at `start` (must fall while CS is high)."""
       times, levels = console._times[console.cs_pin], console._levels[console.cs_pin]
       k = next(i for i, t in enumerate(times) if t > start)
       assert levels[k - 1] == 1 and times[k] > start + width
       times[k:k] = [start, start + width]
       levels[k:k] = [0, 1]
   ```
   Before the first transfer, use `start=4`; the first transfer starts at t = 16. Between transfers, use `start=console.cs_rises[n] + 5`.
2. **Positive tests:** with a glitch before the first transfer, and between skipped transfers, `delivery_problems()` is empty.
3. **Negative test,** in the style of `NegativeTest`: patch `jmp x-- skipped` back to `jmp x-- start` (the v0.10 program) and check that the glitch case reports a shifted payload. This shows the test can catch the bug.

**Fix 1 is a C change,** so the simulator can't test it. It needs a hardware check.

## 6. Checking on hardware

- **Without counters:** PicoBoot-DX can't log a glitch. The practical check is many cold boots: 50 or more, at least 10 s off each, counting blue screens.
- **For comparison:** with v0.10 on the owner's DOL-001, expect about 1 in 28. In PicoBoot-WiFi's 50 pull-up boots there were none.
- **For details:** the PicoBoot-WiFi measurement build (`hwtest/dx-measure`, Pico 2 W only) logs the CS level at start-up, the raw and clocked CS counts, and the time of each first edge, all readable over Wi-Fi.

## 7. What stays the same

- **Payload format, address and pins:** unchanged.
- **Payloads:** existing ones keep working.
- **`clocked_output`:** unchanged.

## Sources

- PicoBoot-WiFi phase 0 results: `~/GitHub/PicoBoot-WiFi/docs/research/phase0-results.md` (sections 0.3, 0.3b, 0.7)
- PicoBoot-WiFi design decision D36: `~/GitHub/PicoBoot-WiFi/docs/architecture.md`
- [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf): Appendix D.5.1 (RP2350-E9), pad DC characteristics (pull-up 32–86 kΩ, pull-down 36–113 kΩ at 3.3 V)
- [webhdx/PicoBoot `src/pio.c`](https://github.com/webhdx/PicoBoot/blob/main/src/pio.c): the same pin set-up, no pulls
