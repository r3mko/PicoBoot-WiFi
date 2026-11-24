# Length-based injection: results (D47)

The results of D47's feasibility stage: the CLK capture series (plan step L4), what it means for the length detector (step L5), the length test build's series (step L8), and the owner's decision (step L9). **[inf]** marks a conclusion rather than a measured or sourced fact.

**Status:** L4 and L5 approved by the owner with plan v1.30 (2026-11-23); L8 and L9 with plan v1.32 (2026-11-24).

**Short answer:** CLK leaves the detector room on both consoles.
- **The detector's need:** it looks at CLK every 3 samples (12 ns at 250 MHz), so it needs each high phase to last at least 3 samples and each low phase at least 2 ([length-injection.md](length-injection.md) §3, L3's review) [inf].
- **The phases:** in 20 boots, 430,060 high and 425,600 low phases in BS1, none lasted under 4 samples (16 ns).
- **So the detector stays as L1 built it.**

The captures also measure the EXI clock exactly: 27.00 MHz in BS1, with a pause and a stretched clock every 8 bytes in the first block.

**And the length method boots the console** (L8): on the dev console, the length test build started its payload on all 13 power-ons, quick power cycles included, with the counts, margins and injection times of the count (§6). The main console wasn't run, the owner's choice.

**The decision** (L9, the owner): one release, 1.2.0, with the CS wait in today's injection and the length method as an opt-in, the count staying the default (§7).

## 1. The series

**The test build:** `1.1.3-dev+60925ce`, from `diag-cs-timing` (L2, L3's fixes), with release 1.1.2's page. It injects with D36's count and captures CS and CLK every 4 ns from BS1's first clock, for 1.05 ms.

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.1.3-dev-update.bin` | `bcf57c1bc288292fc9dd738439c98abf4f0e01121375600f5d300cd61a376652` |
| `picoboot-wifi-1.1.3-dev.uf2` | `8dfe4765acea2d191ce6843cd7896a65bf37a3492ee34d93af1028ae46bfcce6`, not installed |

**The consoles,** both on clean wiring, the owner's choice:
- **the dev console:** the quick-solder flex cable;
- **the main console:** the other Pico, new wires and the CubeSB board at U10 ([main-console-tv.md](main-console-tv.md)).

**The series:** on each console, the update over Wi-Fi, kept by its first power-on, then 10 power-ons.
- **The boots:** 5 with the built-in gekkoboot, then 5 with Swiss r2174 (885 transfers). These are log boots 2–11; boot 1 is the update's restart.
- **The downloads:** the raw capture of log boots 2, 6 and 11, and the boot log at the end.
- **Every power-on started its payload** (the owner). Both consoles went back to 1.1.2 afterwards.

## 2. The checks

| Check | Dev console | Main console |
|---|---|---|
| Every power-on | complete; 285 or 1110 raw and clocked, exact | the same |
| The capture | 262,144 samples, 225 transfers, 21,503 BS1 edges on every boot | the same |
| CS at "PIO ready" | low on every boot | high on every boot |
| Margin (clocked, ms) | 160.3–164.3 | 154.1–154.9 |
| The on-console summary | equal to the host's C summary of each downloaded capture | the same |

## 3. CLK's phases in BS1

Whole phases, counted per boot by length in samples of 4 ns. The ranges are over the 10 boots:

| Phase | Console | 1–3 samples | 4 (16 ns) | 5 (20 ns) | 6 (24 ns) | 7 or more |
|---|---|---|---|---|---|---|
| High | dev | 0 | 627–701 | 20,801–20,875 | 1–5 | 0 |
| High | main | 0 | 2,345–2,437 | 19,066–19,158 | 0 | 0 |
| Low | dev | 0 | 15,069–15,139 | 6,141–6,211 | 0 | 0 |
| Low | main | 0 | 13,349–13,446 | 7,834–7,931 | 0 | 0 |

- **A period is 9 or 10 samples:** 9.26 on average. A high phase is mostly 5 samples and a low mostly 4. The main console's are a little more even.
- **No phase under 4 samples** on either console: 215,030 high and 212,800 low phases per console.
- **Each phase is quantized to the 4 ns grid,** so a 4 is a true length of 16–20 ns [inf]. The detector's 12 ns poll then has 4–8 ns to spare.

## 4. What the raw captures show

The three downloads from each console agree to the sample, and the two consoles with each other, as T3 found for CS ([cs-timing-results.md](cs-timing-results.md)).

**BS1:**
- **Each of the 224 fetches is 96 clocks without a pause:** a 32-bit command and 8 bytes ([gc-docs.md](gc-docs.md) §2).
- **The clock is 27.00 MHz:** 9.259 samples, 37.04 ns, a period on every boot. That is YAGCD's bus clock of about 162 MHz divided by 6 [inf].
- **CS falls 18–19 samples (72–76 ns) before the first clock,** and rises 16–17 samples (64–68 ns) after the last one falls.
- **CS is low for 919–920 samples (3.676–3.680 µs).** T3's recorder read 3.656–3.664 µs, within the 8–20 ns it reads short.

**The first block** (transfer 225), as far as the capture reached, 2,389 clocks:
- **CS falls 78–79 samples (312–316 ns) before its first clock.**
- **CLK pauses after the 32 command clocks:** a low of 165 samples, 660 ns, before the data clocks start.
- **The data clocks run at BS1's 27.00 MHz,** but one period in every 64 is stretched to 12–13 samples (about 50 ns): every 8 bytes, a DMA's transfer size [inf]. The first block's data clock averages 26.86 MHz.
- **That's what T3 measured:** its ~26.8 MHz came from fitting the 1 KB blocks with continuous clocks ([cs-timing-results.md](cs-timing-results.md) §2), so it measured the block average.

## 5. For the detector and the switch (L5)

- **Counting:** every BS1 phase lasts 4 samples or more, so the detector's poll sees every edge [inf]. It reaches 96 in each fetch and never a 97th. In the first block, the 97th clock comes after the pause and the 32 command clocks, so a pause only makes the count wait.
- **The hand-over at bit 66:**
  - **Where the stretched period falls:** the 64-clock stretch is the low phase just before the first block's 97th clock (clock 96, counting from 0), at the 8-byte boundary where data bit 64 starts.
  - **What follows it:** the periods after it are ordinary, 10 samples and then 9. So the switch has a normal period between clock 97, after which the Pico drives bit 66, and clock 98, where the console reads it.
  - **The margin:** in the simulator the bit goes on the line 3 cycles after clock 97 rises ([length-injection.md](length-injection.md) §3, L1). That leaves about 6 samples (24 ns) before clock 98 [inf].
- **No adjustment needed:** the detector and the output program stay as L1 built them, for the length test build (L6).

**Not shown:** noise. Both consoles have clean wiring now, the owner's choice for this stage, so nothing here tests the method's benefit.

## 6. The length test build on a console (L8)

**The test build:** `1.1.3-dev+c3503a4`, from `length-injection` after its code review (L6, L7). It injects by length, and is otherwise release 1.1.2.

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.1.3-dev-update.bin` | `ec6df5732f13d7a105b7e6883347d88288d170a8ee7a72d169922abfbad2d2a4` |
| `picoboot-wifi-1.1.3-dev.uf2` | `09645c992d91eb3c96f70d623efebbc0958e61da6be21b9e5924c246f8d44b28`, not installed |

**The series, on the dev console** (log boots 24–37):
- **Boot 24:** the update's restart, a warm boot into slot B on trial.
- **Boots 25–34, cold boots:** 5 with the built-in and 5 with Swiss r2174. Boot 25 kept the trial.
- **Boots 35–37:** 3 quick power cycles, with the built-in.
- **Every one of the 13 power-ons started its payload** (the owner).

| Check | Result |
|---|---|
| Every power-on | complete; 285 or 1110 raw and clocked, exact; the method recorded as `length` |
| Margin (clocked) | 160.9–163.9 ms, as on 1.1.2 |
| Injection time | 19,420–19,421 µs with the built-in and 273,283 µs with Swiss: the count's own times, as in L4. The same log's last Swiss boot on 1.1.2 (boot 23) took 273,282 µs |

**What it shows:**
- **The length method boots the console.** The detector found the first block on every boot, the quick power cycles included, and the payload arrived whole.
- **BS1 ignores the first block's first bytes,** shown on hardware. On every boot U10, not the payload, sent the first 66 bits. That rested on an inference and the Shuriken attack until now.
- **The rest of the read is unchanged:** the same counts, margins and injection times as the count.

**Not run, the owner's choice: the main console.** Its reasons [inf]:
- L4 measured the same clock and CS timing on both consoles, to the sample, so the detector sees the same signals.
- No BS1 can use the first 32 bytes: every PicoBoot version has sent the raw, unscrambled `IPLBOOT ` header there, which the console descrambles into garbage, and that works on every console.
- The main console holds CS high from the start, the simpler case for the detector. The dev console's low start passed.
- A release with the length method would have its console test on both consoles anyway.

**Not shown, as planned:**
- **The benefit:** both consoles have clean wiring, so nothing here tests the length method against noise in BS1.
- **Its new exposure:** a CLK glitch during BS1 would start the payload early (L7, [length-injection.md](length-injection.md) §4).
- So both the benefit and the risk stay reasoned, not measured.

## 7. The decision (L9)

**The owner's choice (2026-11-24): one release, 1.2.0, with both.**
- **Today's injection gets the CS wait,** L1's finding: one instruction before each block's IRQ, so a CS blip that splits a payload block no longer shifts every later block. Every boot gains it, whatever the method.
- **The length method becomes an opt-in,** a persistent choice on the Payloads tab: "Injection: standard / length-based", with standard, D36's count, the default.

**What that weighs:**
- **For:** the method boots the console (L8), and it ignores the noise in BS1 that breaks the count ([length-injection.md](length-injection.md) §4).
- **Against:** its benefit is untested here, since both consoles have clean wiring, and one CLK glitch in BS1 starts it early (L7).
- **As an opt-in, standard the default,** that risk applies only to consoles switched to it. A failed length boot still brings Wi-Fi up, so the user switches back on the page.

**Next:** 1.2.0's own design questions, design and plan: the switch (the research's §5 and §7), where the code goes, and its console test on both consoles.
