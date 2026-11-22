# A diagnostic build for CS timing

What a test build could measure about the console's CS line during the IPL read, how it would do it without touching the injection, and what its numbers would decide. Research for the owner's next step after release 1.1.1: making the 224 skipped transfers tolerant of noise on CS, or injecting by address with a fourth wire. **[inf]** marks a conclusion rather than a sourced or measured fact.

## 1. What the boot log can't tell today

- **Where an extra CS transfer fell.** The counters count every CS low period, real or not. An extra one during BS1 sends the payload one transfer early, while one during the payload is most likely harmless ([main-console-tv.md](main-console-tv.md) §1). The counts look the same either way.
- **How short a noise blip on CS is,** and how long the real gaps between transfers are. A filter that ignores short CS-high blips needs both numbers.
- **What the console did at transfers 225 and 226** on the stalled quick power cycles. It stopped after two short transfers, but the log only has the totals ([main-console-tv.md](main-console-tv.md) §4).
- **CLK's level at each falling CS edge.** D36's known limit is a false CS edge while CLK already reads high. "Not seen" rests on 3 glitches caught.

## 2. The timing, from the existing logs

`injection_us` runs from the first CS edge to the end of the injection. Fitted over four payloads on the second console:

| Payload | Blocks | `injection_us` | Fit: 958.6 µs + blocks × 307.71 µs |
|---|---|---|---|
| The built-in gekkoboot | 60 | 19,421 | 19,421 |
| cubiboot with D43's stub | 717 | 221,588 | 221,587 |
| Swiss r2174 | 885 | 273,283 | 273,282 |
| Swiss r2119 | 1319 | 406,831 | 406,831 |

- **BS1** (224 transfers of 32 address and 64 data clocks) takes about 0.96 ms: 4.28 µs per transfer, gap included.
- **A 1 KB block** (32 + 8192 clocks) takes 307.7 µs, gap included.
- **The EXI clock** can be 1, 2, 4, 8, 16 or 32 MHz (YAGCD chapter 5, the EXI channel's CLK field, https://gc-forever.com/yagcd/chap5.html). Only 32 MHz fits both [inf]:
  - a BS1 transfer would take 3.0 µs, which leaves a CS-high gap of about 1.3 µs;
  - a block would take 257 µs, which leaves about 50 µs;
  - at 16 MHz, a BS1 transfer alone would take 6.0 µs, more than the 4.28 µs measured.
- **So a filter for CS-high blips** would have to stay well under about 1.3 µs [inf]. The real gaps, and how short the noise blips are, need measuring.

**Corrected by the measurements ([cs-timing-results.md](cs-timing-results.md) §2):** the clock is about 26.8 MHz, not 32 MHz, and BS1's gaps are about 150 ns, not 1.3 µs. The fit above holds, but its split into transfers and gaps doesn't: BS1's 0.96 ms includes a 93 µs pause, and a block's 307.7 µs is nearly all CS low.

## 3. The recorder

A third passive state machine in pio1, next to `cs_raw` and `cs_clocked`. It times every CS-high and CS-low period, 2 cycles per count (8 ns at the injection's 250 MHz):

```
.program cs_timing              ; JMP_PIN = CS; autopush at 32 bits
.wrap_target
    mov y, ~null
high:
    jmp pin high_count          ; CS still high: count
    in y, 16                    ; CS fell: the CS-high time
    mov x, ~null
low:
    jmp pin low_end             ; CS rose: done
    jmp x-- low
low_end:
    in x, 16                    ; the CS-low time; one word per transfer
.wrap
high_count:
    jmp y-- high
```

- **Size:** 8 instructions. pio1 has 32, and the counters use 10.
- **Output:** one 32-bit word per transfer (CS-high time, CS-low time, 16 bits each, as 0xFFFF minus the count). A DMA channel copies them from the RX FIFO into a RAM buffer.
- **Range:** 16 bits at 8 ns cover 524 µs, which is enough for a 257 µs block. The periods before the first transfer (about 160 ms in all) wrap around and are ignored, except that the dev console's false edge would show as a short CS-low period among them, with its width.
- **The buffer:** 4096 words (16 KB) cover BS1, the largest payload (1536 blocks) and plenty of extras. It lives in `.uninitialized_data` like the other big buffers, so nothing is zeroed before the injection (the phase 4 lesson). RAM has about 400 KB free. The DMA stops when the buffer is full.
- **Optional, for D36's known limit:** give 1 bit of the CS-high time to CLK's level at the moment CS falls (`in pins, 1` with CLK as the IN pin). That would show how often CLK is high at a CS fall, real or false.
- **Start and stop:** with the counters, before "PIO ready", and stopped with them after the snapshot. Like the counters, it only reads CS (and CLK), so it can't disturb the injection. pio0 isn't touched.
- **The bus:** one word per transfer, at most one every 4.28 µs. The injection's DMA keeps its priority (D27).
- **Cost before "PIO ready":** setting up one more state machine and DMA channel, a few microseconds [inf]. `main_to_ready_us` would show it; the margin is 155 ms or more.

**What it can't see:**
- **Blips shorter than about one PIO cycle** (4 ns) may slip through its input synchroniser. The counters, behind the same synchronisers, did count the blips at the TV, so those lasted at least that long [inf].
- **The number of clocks per transfer:** one state machine can wait on CS or on CLK, not on both. The CS-low time stands in for it (3.0 µs for a BS1 transfer, 257 µs for a block).
- **Anything the console does on `EXI.IN`,** such as the address of each transfer. That needs the fourth wire (plan v1.8).

## 4. What the firmware does with it

After the injection, before Wi-Fi starts, the firmware would compute a summary from the buffer:
- the shortest and the longest CS-high gap during BS1 (transfers 1–224), and during the payload;
- the number of CS-high gaps shorter than, say, 500 ns (blips), and the transfer number of the first one;
- the CS-low time of transfers 225 and 226: about 257 µs for a block, about 3 µs for a BS1-sized transfer. A short 225th means the injection started early, on BS1's last real transfer;
- with the CLK bit: how many CS falls came with CLK high.

**Surfacing it, in a test build only:**
- the summary on the test page;
- the raw buffer of the current boot as a download, for analysis on the Mac. It's in RAM, so it's lost at the next power-on: one download per boot.
- **Optionally,** the summary of each boot in a separate flash log, so a series can be read in one go, as the boot log does for the counts.

## 5. What it would decide

- **The gap filter:** if the real gaps in BS1 are always about 1 µs and the blips are tens of nanoseconds, a CS-high minimum of, say, 200–500 ns can merge the blips without touching real transfers. That would have made the second console's marginal wiring boot [inf]. If the two overlap, a filter can't work, and the fourth wire is the way.
- **The boot log, for releases:** the shortest CS-high gap of each boot is a wiring-quality number. Shown on the Diagnostics tab, it would warn of a marginal install before it fails.
- **D36's known limit:** the CLK bit shows how close CLK-high-at-a-CS-fall comes in practice.

## 6. The test series

- **Both consoles,** cold boots with the built-in and with Swiss. The two have different CS starts and different wiring (the dev console's flex, the second console's CubeSB and 24 AWG wires).
- **To see blips,** they have to happen. The second console's old wiring is gone, so a deliberately poor wire is the only way back: a longer, thinner ground wire, for example. That's the owner's call.
- **The quick power cycles** that stalled on the second console, to see transfers 225 and 226.
