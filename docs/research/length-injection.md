# Finding the first block by its length (the Shuriken method)

Whether PicoBoot-WiFi could find the injection point the way the Shuriken attack does, by a transfer's length instead of a count of 224, as a second method the user can switch to (the owner, 2026-11-23). The Shuriken attack itself is in [gc-docs.md](gc-docs.md) §3. **[inf]** marks a conclusion rather than a measured or sourced fact.

**Short answer:** yes, it looks possible with PicoBoot's wiring and payloads, without lifting a pin or changing the payload format [inf]. And it would guard BS1, one of the two parts of the boot that noise breaks today.
- **Noise breaks today's boots in two places** (§2):
  - extra CS transfers during BS1 send the payload early;
  - extras that split a payload block make every later block one bit late, which plan step L1 found in the simulator.
  
  The length method guards BS1. A one-instruction wait for CS high guards the payload, for either method.
- **A length-based start ignores what happens during BS1:** pulses without clocks, split transfers, and a clock that's already high at the false edge, D36's known limit.
- **The cost:**
  - a second set of PIO programs, likely over two PIO blocks;
  - a polling loop with a tight timing margin;
  - a setting with its page, API and CLI parts;
  - console tests in both modes.
- **Showing the benefit on a console needs noise during BS1:** marginal wiring like the TV's. On clean wiring both methods should boot every time.

## 1. The Shuriken method

From its VHDL ([gc-docs.md](gc-docs.md) §3):
- **It counts clocks within each CS-low period,** from 0 every time CS goes high.
- **The first transfer that passes 255 clocks is the first IPL block.** BS1's fetches are 96 clocks: a 32-bit command and 8 bytes.
- **In that transfer it switches the Flipper's data input to its serial flash at clock 287,** so the block's first 32 bytes come from U10.
- **In every later block it switches right after the 32-bit command.**
- **It lifts U10 pin 9.** Its CPLD is the only driver of the Flipper's data input, and passes U10's output through when it isn't injecting.

The 255 is the Shuriken board's own choice: its flash needs a 4-byte read command, which the CPLD sends during the block's first 32 bytes. Any point after BS1's 96 clocks would tell the transfers apart.

## 2. Why the count is the weak point

**How PicoBoot-WiFi injects today** (`src/firmware/pio/injection.pio`, `src/firmware/injection.c`):
- `on_transfer` counts 224 clock-qualified transfers (D36), then raises IRQ 0 at every CS fall.
- `clocked_output` waits for IRQ 0, then counts the 32 command clocks and shifts out 8192 payload bits, one per rising clock edge. The DMA feeds it the stored payload from its first word.

**Noise during the payload transfers moves the output too** (found in plan step L1, in the PIO simulator; this note first said it didn't).
- A CS blip in the middle of a block sets IRQ 0 early. When the block ends, `clocked_output` finds the flag set and starts counting command clocks at once, before the console's last clock of the block.
- That clock counts as the next block's first command clock, so every later block arrives one bit late.
- **The fix:** a wait for CS high before each block's IRQ. The block is over only when CS rises, and no clock runs in the gap. One instruction, for either method (L1).
- **The TV boots fit this too:** boot 95, with no extras, started, and "a couple" of the 21 did ([main-console-tv.md](main-console-tv.md) §1).

**Noise during BS1 moves the start.** One clocked extra among the 224 sends the payload one transfer early, and the console starts its own menu. D36 ignores pulses without clocks, but:
- not split transfers, which are clocked;
- not a false edge while CLK already reads high (D36's known limit).

## 3. How it would map onto PicoBoot-WiFi

**No pin to lift.** PicoBoot already drives pin 9's line against U10's output ([gc-docs.md](gc-docs.md) §3, point 3). Taking the line over partway through the first block is electrically the same as taking it over after the command, as today [inf].

**No payload change.**
- **The first 32 bytes:** the DMA sends the stored payload from its first word, so today the console's first 32 bytes of the first block are the payload's raw, unscrambled `IPLBOOT ` header. The Flipper descrambles them into noise, so BS1 can't be using them [inf].
- **Where they land:** unscrambled, the image starts after a 32-byte header and runs from `0x81300000` ([gekkoboot-dol.md](gekkoboot-dol.md) §2). Shuriken lets U10's own bytes through there.
- **So:** a switch anywhere inside the first 32 bytes replaces only header bytes, whoever sends them. The output program drops the header bits U10 already sent from its FIFO (`out null`), and the DMA stays as it is.

**The detector** (a sketch, not tested): one state machine, with `JMP_PIN` = CS and `IN_BASE` = CLK with `IN_COUNT` = 1. That makes `mov y, pins` read CLK alone, which only the RP2350 can mask this way.

```
start:
    wait 1 gpio CS          ; between transfers
    wait 0 gpio CS          ; a transfer starts
    mov x, isr              ; 96: a BS1 fetch's clocks (preloaded)
low:
    jmp pin start           ; CS high before a 97th clock: not a block
    mov y, pins             ; y = CLK
    jmp !y low              ; CLK low: look again
    jmp x-- high            ; count the rising edge...
    irq set 0               ; ...the 97th, with CS still low: the first block
blocks:
    wait 1 gpio CS          ; every later transfer, as on_transfer does now
    wait 0 gpio CS
    irq set 0
    jmp blocks
high:
    wait 0 gpio CLK         ; a rising edge is always followed by a fall before CS rises
    jmp low
```

- **Why it polls:** the detector must notice CS going high while it waits for a clock. A blocking `wait` on CLK would carry the count into the next transfer after a short one, which is exactly the split-transfer case.
- **The timing at the injection's 250 MHz** (4 ns per instruction), against an EXI clock of 26.76 MHz:
  - the `low` loop looks at CLK every 12 ns, shorter than the clock's high phase of about 18.7 ns, so it can't miss an edge [inf];
  - a BS1 fetch's CS is low about 75 ns longer than its 96 clocks ([cs-timing-results.md](cs-timing-results.md) §2), and then high for at least 136 ns before the next fetch. The poll sees it within 12 ns of rising, well inside the gap.
- **The margin is thin:** 12 ns against 18.7 ns assumes an even duty cycle, which nobody has measured. The D44 recorder didn't time CLK's phases.

**The output program** needs an entry for the first block:
- wait for the detector's IRQ;
- drop the header bits U10 has sent;
- get ready, drive the rest of the block, then carry on as `clocked_output` does.

**Where in the first 32 bytes to switch:**
- **The earliest point** is right after the 97th clock, at data bit 65.
- **The catch:** the output program needs several instructions after the IRQ (dropping bits, setting a count, turning the pin to an output). That's about 30 ns at 250 MHz, against 37 ns to the next clock. So a later point, a few clocks on, or byte 32 like Shuriken, leaves room [inf].
- **The cost of a later point:** the skip needs a count of its own. The registers are tight: x counts, y holds 8191, ISR one more constant.

**The PIO budget:**
- pio0 holds `on_transfer` and `clocked_output`, 26 of its 32 instructions (10 and 16).
- The detector is 14 instructions; the output program with its first-block entry 26 (L1).
- Loading only the chosen method's programs at boot keeps the two methods apart. But the length method alone is over 32, so its detector would go in pio1, next to the passive counters (10 instructions). It would raise its IRQ in pio0 with the RP2350's `irq prev` [inf].

## 4. What it guards against, and what it doesn't

| Case | Count of 224 (today) | By length |
|---|---|---|
| The dev console's false edge, CLK low | ignored (D36) | ignored: no clocks |
| A false edge while CLK reads high | counted: D36's known limit | ignored: no rising edges |
| Noise that splits BS1 transfers (the TV) | counted: the payload goes early | ignored: each piece has under 97 clocks [inf] |
| A CS blip in the first ~97 clocks of the first block | — | the switch lands at the wrong bit, a window of about 3.6 µs. L1: only a blip the detector sees, while CLK is low |
| Noise that holds CS low across a gap, merging two BS1 fetches | merged: the count falls short, so the payload goes one transfer late [inf] | 192 clocks: taken for a block, so the payload goes early [inf]. Not seen |
| Noise that splits a payload block | every later block one bit late (L1) | the same, unless each block waits for CS high, as L1's prototype does |
| An IPL whose BS1 isn't 224 fetches | breaks | works [inf]. None known: every console recorded shows 224 |

**From T3's records:** in all 33 power-ons, every one of the first 224 CS-low periods lasted 3.656–3.664 µs, and the 225th was a full block ([cs-timing-results.md](cs-timing-results.md)). So on these consoles, a 97-clock threshold separates the two cleanly.

## 5. A switch for the user

**As the owner asks: a choice like the firmware slot and the payload.**
- **Where it would be kept:** next to the payload choice, read at boot before the injection starts. The injection starts about 0.4 ms after `main()`, and both methods have to be ready by then.
- **The page:** a choice on the Payloads tab or in Settings: "Find the payload's start: by count (default) / by length".
- **The API and the CLI:** a field for it.
- **The diagnostics:**
  - **"This boot":** shows which method ran.
  - **The boot report:** a byte for it, in the report's padding, without a format change (checked: §7).
  - **The noise warning (D45) and the counters:** stay as they are. The counters are passive and count the same transfers either way.
- **A safety net:** if the length method fails on a console, the console shows its own menu, and Wi-Fi still comes up. The user switches back on the page. A "once" option, like "original IPL once", would make trying it safer still.

## 6. Testing it

- **The PIO simulator** (`tests/pio_sim.py`) needs:
  - `mov` from pins with `IN_COUNT`;
  - `jmp !y`;
  - IRQs across PIO blocks, if the detector goes in pio1.
  
  Then cases for:
  - a clean boot;
  - the dev console's false edge;
  - a false edge with CLK high;
  - split BS1 transfers;
  - a blip early in the first block;
  - noise during the payload;
  - the switch timing.
- **The consoles:**
  - both methods on both consoles, cold boots with the built-in and Swiss;
  - the update and the setting surviving it.
- **The benefit:** shows only with noise during BS1, such as the second console's old loose wires or a long ground wire. On clean wiring both methods should boot every time. The owner ruled deliberately poor wiring out for T3.

## 7. What it would take

Checked against the code on 2026-11-23.

**First, whether the detector works.** Three cheap steps, before any firmware change:
1. **A simulator prototype.**
   - **The additions:** `tests/pio_sim.py` gains `mov y, pins` with `IN_COUNT`, `jmp !y`, and an IRQ to the neighbouring PIO block (`irq prev`).
   - **The programs:** the detector, and the output program's first-block entry.
   - **The tests:** the cases in §6.

   No firmware change, and no console.
2. **CLK's high and low phases.** The poll assumes CLK stays high for about 18.7 ns (§3).
   - **With a logic analyser or scope** at 200 MS/s or more, one look settles it.
   - **Without one,** a variant of the D44 recorder on `diag-cs-timing` could time the phases in PIO, at 4 ns.
3. **The switch point.** At data bit 65 the output program has about 30 ns of the 37 to the next clock. A later point needs a skip count, and the registers are full (§3). The simulator would settle which.

**Then the build:**

| Part | What it needs | Checked |
|---|---|---|
| The detector | 14 instructions in pio1 (L1) | pio1 holds only the passive counters, 10 of its 32 instructions (`cs_raw` and `cs_clocked`, `src/firmware/counters.c`). Wi-Fi's driver takes pio2 after the injection ([injection-end.md](injection-end.md)) |
| The output program | `clocked_output` with a first-block entry, 26 instructions in pio0 (L1), loaded only when the length method is chosen | pio0 holds 26 today (`on_transfer` 10, `clocked_output` 16) |
| The DMA | No change: the output program drops the header bits U10 has sent | `injection.c` sends the stored payload from its first word |
| The setting | A bit in boot control's `flags`, next to "original IPL once" (bit 0) | Bits 1–7 are free (`src/common/bootctl.h`), so the format stays. The loader copies the record's first 12 bytes, flags included, but decides on the slot fields alone, so it needs no change. `main.c` loads boot control before the injection and already reads the flags there |
| The API | A field like `original_ipl_once`: in `/api/status`'s payloads, and in the request that sets it | `payloads.c`, `api.c`, `docs/api.md` |
| The page | A persistent choice on the Payloads tab (the owner's choice) | `src/web/index.html` |
| The CLI | An option to set it | `tools/picoboot-wifi.py` |
| The boot report | One byte for the method, in the padding, so the format stays | See below the table |
| The diagnostics | "This boot", the boot log and the test page show the method | `index.html`, `page.c` |
| Tests | The simulator cases; host tests for the flag and the API | `tests/` |
| Console tests | Both methods on both consoles, cold boots with the built-in and Swiss; the setting surviving an update; clean wiring only (the owner's choice) | — |
| Docs | A design decision, a plan section, the README's features and recovery table, release notes | — |

**The boot report's padding, checked:**
- **Where it is:** bytes 29–31 and 57–59, confirmed with the firmware's compiler: short enums, 60 bytes.
- **What existing entries hold there:** `report` is a static in `main.c`, so its padding starts as zeros. A method byte with 0 for "by count" would read right in every entry so far [inf].
- **Which bytes to use:** byte 57 held 1.1.0's `cs_pulldown_test`, set in entries logged with the CS test mode on. So bytes 29–31 are the safer place.

**Risks:**
- **The poll's timing margin,** until CLK's phases are measured.
- **The IRQ's delay from pio1 to pio0** at the first block's switch [inf].
- **The simulator's limit:** it models the input synchronisers, but passing there doesn't prove the timing on hardware.
- **What the console tests can show:** on clean wiring both methods should boot. So they'd show it works as well, not that it works better.

## 8. Options

| Option | What | Effort | Value |
|---|---|---|---|
| A | Research only, for now | none | — |
| B | A test build first, as D44's was: the length method only, on a branch, tried on both consoles | the PIO programs, the simulator, a test build | shows it works on real consoles before anything ships |
| C | A release with the switch: the count stays the default, by length as the option | B, plus the setting, the page, the API, the CLI, the report byte, docs | users with marginal wiring can boot anyway, while the D45 warning still points at the wiring |
| D | Replace the count with the length method | as C, without the switch | not before C has been used widely |

**The owner's choices (2026-11-23):**
- **A, research only for now.**
- **If it's built later:**
  - the switch goes on the Payloads tab;
  - it's a persistent choice, without a "once" option;
  - the console tests use clean wiring only, so the benefit stays reasoned, as for T3.
