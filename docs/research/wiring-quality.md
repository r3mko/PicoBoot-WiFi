# A wiring-quality indicator

How a release could warn of wiring that picks up noise on CS, the owner's choice at the diagnostic build's T5 ([cs-timing-results.md](cs-timing-results.md), plan v1.22, later releases). T5 proposed the shortest CS-high gap of each boot as the number. This note checks what that number would add, and what else could give the warning. **[inf]** marks a conclusion rather than a measured or sourced fact.

**Short answer:** the shortest gap would add little that a release doesn't already measure. Every blip that makes a gap short also makes an extra CS transfer, which the existing counters count. What's missing is the warning itself: the Diagnostics tab shows the counts, but doesn't compare them with the count a clean boot gives. A warning from the existing counts would have flagged both known cases, at no cost in the firmware.

## 1. What noise on CS has looked like

| Case | Wiring | What the counts showed | The boots |
|---|---|---|---|
| A long ground wire (second console, bench; [cs-pull-up.md](cs-pull-up.md) §1) | loose wires, a long GND | tens or hundreds of thousands of transfers (11,492–919,302) | none started |
| At the TV ([main-console-tv.md](main-console-tv.md) §1) | loose wires, short GND | 1–25 extra transfers on 20 of 21 boots, once 77 and once 141 | "a couple" started: those whose extras missed BS1 |
| Clean (both consoles since; [cs-timing-results.md](cs-timing-results.md)) | flex cable; CubeSB with 24 AWG | 224 or 225 plus the payload's transfers, raw equal to clocked | all |

In every case the noise was in the counts. With the TV's wiring, a boot whose extras all fell after BS1 still started, and showed the extras anyway: the warning would have come before the failures it explains [inf].

## 2. What the shortest gap would add

- **The counters see every blip the recorder sees.** Both read CS through the same input synchroniser. The counters' `wait` instructions check it every cycle, while the recorder's loops check every second cycle (pio/counters.pio, pio/timing.pio).
- **A blip that makes a gap short is an extra transfer.** A CS-high blip inside a transfer splits it in two. A CS-low dip inside a gap starts a transfer of its own. Either way, the raw count goes up, and so does the clocked count if the clock runs then. That's how the TV's extras were clocked.
- **So a shorter gap would come only together with extra transfers** [inf]. The shortest gap would tell how short the blip was, which doesn't change what to do about it.
- **The one thing it might show earlier:** if marginal wiring shortened the measured gaps before any blip crossed the threshold, for example through slow edges. That's not shown anywhere. The recorder only ever ran on clean wiring, where the gaps were the same 136 ns on both consoles. The diagnostic build on `diag-cs-timing` could measure it on a deliberately poor wire, which the owner ruled out for T3.

**Its cost, if built anyway:**
- the recorder's 10 instructions in pio1;
- either the CPU draining its FIFO during the injection, which gives false short gaps if it ever falls behind and the recorder stalls, or a DMA buffer;
- a field in the boot log. The report's 3 bytes of tail padding (bytes 57–59, zero in 1.1.1's entries) could hold a 16-bit value without a format change [inf: from the struct layout the T2 review read; to be checked].

## 3. What a release shows today

- **The Diagnostics tab** (`src/web/index.html`) shows "CS edges raw / clocked", and flags only a glitch (raw above clocked: "Glitch ignored by the injection"). The boot log table shows raw/clocked and the payload's transfers, without a judgement.
- **The README's recovery table** asks the reader to compare: "Far more CS edges than expected (225 more than the payload's transfers, so 285 for the built-in gekkoboot) point at noise on CS".
- **The test page** (test builds) compares with 224 or 225 plus the payload's transfers, and says "MISSED" below that. It also says "ok" above it, where noise is.
- **So the data is there, but nothing reads it for the user.** At the TV, the tab showed 1127/1127 and 287/287 next to "complete", with no warning.

## 4. A warning from the counts

- **The rule:** a clean boot counts 224 or 225 plus the payload's transfers. That's every clean boot recorded, hundreds of them, with the built-in, Swiss r2119 and r2174, and cubiboot, but one. More than 225 plus the payload's transfers means noise on CS.
  - **The exception:** boot 108 on the second console's fixed wiring, the built-in, complete with 5368 transfers, most likely the console restarting its reads ([main-console-tv.md](main-console-tv.md) §4). The rule marks it as noise. So would a stalled boot whose console reads again within the 10 s time-out [inf]. Found by release 1.1.2's code review.
  - raw above clocked stays a glitch, which the injection ignored;
  - fewer than 224 plus the payload's transfers is a stall, which the result already shows.
- **Where:** the Diagnostics tab, for the current boot and in the boot log table, with a pointer to the README's wiring advice. The page has every field it needs: `payload_words`, `cs_raw`, `cs_clocked` and `result`. It works on the existing log entries too.
- **The cost:** the page only (`index.html`), so no firmware logic, no API change and no boot-log change. The page is built into the firmware (fsdata), so it still needs a release.
- **Not covered:** boots without an injection, and the built-in "original IPL once", which count nothing.

## 5. The README's wiring advice

Upstream PicoBoot's troubleshooting page asks for wires of "approx. 10 centimeters (or 4 inches)" at most, of 26–28 AWG stranded wire, and warns that thick or single-core wire can break or damage the motherboard's pads ([main-console-tv.md](main-console-tv.md) §5). With what the two consoles showed:
- short wires, with each signal next to the ground wire;
- the ground taken at U10;
- solid joints;
- a flex cable or a quick-solder board at U10 as the better choice.

That would replace and extend the README's ground note.

**Taken up in release 1.1.2 (plan v1.23):** upstream's length and gauge, and the flex cable or quick-solder board as the better choice, next to the existing ground note. The other two points stay here.

## 6. Options

| Option | What | Firmware | Value |
|---|---|---|---|
| A | The warning from the counts (§4), and the README's wiring advice (§5) | the page only | covers both known cases |
| B | A, and the shortest gap (§2) | a PIO program, CPU or DMA work during the injection, a boot-log field | adds only the blip's size, unless gaps shrink before blips appear, which isn't shown |
| C | The README's wiring advice only | none | no warning |
| D | First measure marginal wiring with the diagnostic build, then decide on B | a test build from `diag-cs-timing` | would settle whether the shortest gap warns earlier |

**Correction:** at T5 the proposal said the shortest gap would warn "before it causes failures". The counts already do that, as far as the data shows (§1, §2).
