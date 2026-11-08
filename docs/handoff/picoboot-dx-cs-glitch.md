# Handoff to PicoBoot-DX: the CS glitch behind the occasional blue screens

**From:** PicoBoot-WiFi (`~/GitHub/PicoBoot-WiFi`), phase 0 hardware checks, 2026-11-08.
**For:** whoever works on PicoBoot-DX (`~/GitHub/PicoBoot-DX`, v0.10). The owner asked for these fixes to be brought back to PicoBoot-DX.

**In short:**
- **The failure:** PicoBoot's occasional blue screens at a cold boot come from **one false falling edge on CS**, about 42–44 ms after power-on, when the console first drives CS.
- **Why it breaks the boot:** `on_transfer` counts that edge as one of the 224 skipped transfers, so the payload goes out one transfer early and the console crashes.
- **Two independent fixes:**
  1. Pull CS **up** instead of leaving the pads' reset pull-down.
  2. In `on_transfer`'s skip loop, count a skipped transfer only once CLK clocks in it while CS is still low.
- **Status:** both were measured or simulated in PicoBoot-WiFi; details below. Neither is in PicoBoot-DX yet. It's up to the owner whether and how to take them.
- **Revision 2026-11-08:** fix 2 now also checks that CS is still low when CLK rises, after a code review found a case the first version missed.

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

## 4. Fix 2: count a skipped transfer only once it clocks, while CS is low

**`src/picoboot-dx.pio`, `on_transfer`:** among the 224 skipped transfers, a CS falling edge only counts if CLK then rises **while CS is still low**. The 225th transfer's path (X = 0) is checked first and raises the IRQ at once, as now.

```diff
 start:
     wait 1 gpio CS_PIN          ; Wait for CS to go HIGH
     wait 0 gpio CS_PIN          ; Wait for CS to go LOW
-    jmp x-- start               ; Skip 224 CS pulses (X = 224), continue on the 225th
-    irq set 0                   ; Set IRQ flag to start injecting payload
+    jmp !x inject               ; 224 transfers skipped (X = 0): this is the 225th, inject now
+    wait 1 gpio CLK_PIN         ; A skipped transfer only counts once it clocks...
+    jmp pin start               ; ...while CS is still low (JMP_PIN = CS): a CS glitch doesn't count
+    jmp x-- start               ; Count the skipped transfer (X > 0 here, so this always jumps)
+inject:
+    irq set 0                   ; Set IRQ flag to start injecting payload
 
 .wrap_target
     wait 1 gpio CS_PIN          ; Wait for CS to go HIGH
```

**`src/pio.c`, `on_transfer_program_init()`:** set the jump pin to CS, before `pio_sm_init()`:

```c
    // JMP PIN reads CS: a skipped transfer only counts if CLK rises while CS is still low
    sm_config_set_jmp_pin(&c, cs_pin);
```

**How it works:**
- **A real skipped transfer:** CS falls, CLK rises with CS still low, and X is decremented. That's the same count as v0.10.
- **A CS glitch** (low, then high again without clocks): the program waits at `wait 1 gpio CLK_PIN`.
  - If the next CLK rise comes from the next real transfer, CS is low, and the glitch and that transfer count once together.
  - If CLK rises while CS is high (another EXI device on the shared clock, or a transient), `jmp pin` sees CS high, nothing is counted, and the program waits for CS again.
- **The 225th transfer:** `jmp !x inject` raises the IRQ at its CS edge, without waiting for CLK. So `clocked_output` still sees every address clock.
- **No C constant changes:** `pio_prepare_transfer(..., 224, pio_x)` stays as it is.

**Why not just a CLK wait:** an earlier version of this note had only the CLK wait (`jmp x-- skipped` / `skipped: wait 1 gpio CLK_PIN; jmp start`). A code review found the flaw: a glitch followed by a CLK pulse while CS is high still counted as a transfer. The simulator confirms it (last row below).

**Assembled** with `pioasm` (SDK 2.3.1): `on_transfer` becomes 10 instructions instead of 7. Check that the W builds' CYW43 PIO program still fits next to it in `pio0`; if not, it goes to another PIO block.

**Verified in PicoBoot-DX's simulator** (`tests/pio_sim.py`, with `JMP !X` and `JMP PIN` added, see §5; one-off script in PicoBoot-WiFi):

| Case | v0.10 | CLK wait only | **This fix** |
|---|---|---|---|
| No glitch, 3 transfers | ok | ok | ok, **identical** to v0.10 |
| No glitch, built-in gekkoboot (60 transfers) | ok | ok | ok, **identical** to v0.10 |
| CS glitch (low 2 cycles, no clocks) before the first transfer | broken ("data line driven during transfer 223") | ok | **ok** |
| CS glitch between skipped transfers 100 and 101 | broken | ok | **ok** |
| CLK pulse while CS is high, no glitch | ok | ok | **ok** |
| CS glitch, then a CLK pulse while CS is high | broken | **broken** | **ok** |

**Known limit:** a glitch between the 224th and 225th transfer would still start the injection one transfer early. That's the X = 0 path, which deliberately doesn't wait for CLK. The glitches seen came about 120 ms before the console starts reading the IPL.

## 5. Tests to add in PicoBoot-DX

1. **`tests/pio_sim.py`:** add the two JMP conditions the fix uses (RP2040 datasheet p.322: `001` = `!X`, `110` = `PIN`), and a `jmp_pin` setting for a state machine.
   ```python
   JMP_ALWAYS, JMP_X_ZERO, JMP_X_DEC, JMP_PIN, JMP_NOT_OSRE = 0b000, 0b001, 0b010, 0b110, 0b111
   # decode(): accept JMP_X_ZERO and JMP_PIN as well
   # StateMachine.__init__(..., jmp_pin=None): self.jmp_pin = jmp_pin   (EXECCTRL.JMP_PIN)
   # _execute(), JMP:
   #     elif a == JMP_X_ZERO: taken = self.x == 0
   #     elif a == JMP_PIN:    taken = pio.synced_input(self.jmp_pin, t) == 1
   ```
   Add unit tests for both conditions in `tests/test_pio_sim.py`.
2. **`tests/test_pio_programs.py`, `simulate()`:**
   - Configure `on_transfer` with `jmp_pin=CS_PIN`, mirroring `sm_config_set_jmp_pin()`.
   - Add an option for extra pulses on the simulated console: a CS low pulse without clocks, and a CLK high pulse while CS is high, either before the first transfer (`start=4`; the first transfer starts at t = 16) or a few cycles after a transfer's CS rise (`console.cs_rises[n] + 3`).
   ```python
   def add_pulse(console, pin, start, width=2, level=0):
       """Drive `pin` to `level` for `width` cycles at `start` (it must be at the other level then)."""
       times, levels = console._times[pin], console._levels[pin]
       k = next(i for i, t in enumerate(times) if t > start)
       assert levels[k - 1] != level and times[k] > start + width
       times[k:k] = [start, start + width]
       levels[k:k] = [level, 1 - level]
   ```
3. **Positive tests:** every row of the table in §4 delivers the payload with `delivery_problems()` empty.
4. **Negative tests,** in the style of `NegativeTest`:
   - with v0.10's `on_transfer` (`jmp x-- start` and no CLK check), the glitch cases report a shifted payload;
   - with the CLK wait but without `jmp pin`, the last row does.

   This shows the tests catch both mistakes.

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
