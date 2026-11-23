# Release 1.1.2 results

The console test of release 1.1.2 (the plan's step R4), run by the owner on 2026-11-22 on both consoles, with the noise warning's hand check and the weak-link test on the Mac. **[inf]** marks a conclusion rather than a measured or sourced fact.

**Status:** R4 passed. Approved by the owner with plan v1.26.

**Setup:**
- **The dev console:** the owner's DOL-001 with the quick-solder flex cable ([main-console-tv.md](main-console-tv.md) §6). Before the test: 1.1.1 in slot A, and T3's test build in slot B. Payload 1: Swiss r2174 (885 transfers, so 1110 CS transfers expected).
- **The main console:** the second DOL-001, with the other Pico, new wires and the CubeSB board at U10 ([main-console-tv.md](main-console-tv.md)). Before the test: a test build in slot B, `1.1.1-dev+…` (the owner; T3's diagnostic build was `1.1.2-dev+52d2603`, [cs-timing-results.md](cs-timing-results.md)). Payload 2: Swiss r2174; payload 1 filled.
- **The built-in gekkoboot** is 60 transfers, so 285 expected.

Boot numbers are each console's boot log, saved after the boots (`/api/diagnostics`).

**Build:** `1.1.2` from `260d79b`, the build after the code review (R3):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.1.2-update.bin` | `a5cce07259153392bb7ccaeb93899db5cc8a28be2bb2229949ffc19fb4abd57a` |
| `picoboot-wifi-1.1.2.uf2` | `973b0e5c03243ec9f666b0ed0c463885a51adf8be558d3b2739b02423226d9de`, from the same build. R4 didn't install it |

## The checks

| Check | Boots | Result |
|---|---|---|
| The update over Wi-Fi from 1.1.1 | dev 4, 5 | **Passed:** into slot B, then its restart, a warm boot, "not run", on trial. Boot 5, a power-on, kept it |
| The update over Wi-Fi from a test build | main 1, 2 | **Passed:** into slot A, then its restart, on trial. Boot 2, a power-on, kept it |
| The built-in gekkoboot | dev 5–7, main 2–4 | **Passed:** 285 raw and clocked every time |
| Swiss r2174 | dev 8–10, main 5–7 | **Passed:** 1110 raw and clocked every time. Also dev 2–3, on 1.1.1 |
| No warning | all | **Passed:** every count is a clean boot's, so no "Noise on CS" and no "+N noise" (the owner) |
| Every power-on | dev 5–10, main 2–7 | Complete, raw equal to clocked (no glitch), no fallback |
| The noise warning by hand | the Mac | **Passed:** the page, given made-up diagnostics with the recorded counts, marks exactly boots 6–9 and 13 (boot 108's case) in the log, and shows each of the five "This boot" notes as expected (the owner) |
| The weak-link test | main | **Passed:** below |

**The main console's log starts at boot 1,** the update's restart: none of the test build's boots are in it. A release reads only entries of its own magic, version and length (`src/common/ringlog.c`). Test builds since D44 write version `0x8004` and longer reports, so 1.1.2's first boot started a log of its own [inf: this fits a test build from `diag-cs-timing`; a test build from before D44 would have shared the log]. The dev console's log starts the same way, at boot 1, a warm boot into 1.1.1 after the test build [inf].

## The weak-link test

The page's polling on a link that loses packets (D46, [weak-link.md](weak-link.md) §3), on the main console:
- **The Mac:** Apple's Network Link Conditioner, a profile "Far" with 80% of packets dropped both ways. Not 100%: then no request reaches the Pico, so nothing gets stuck on it.
- **Each run:** "Far" on for 2 minutes with the page on the Status tab, then off. **A** is the time until the uptime counts up again, without touching the page. **B** is a reload after that.
- **3 runs on each firmware,** with a power cycle before the first run only, the same on both.
- **The baseline:** the test build in slot B. Its page and network code are 1.1.1's: `diag-cs-timing` changes neither `index.html` nor lwIP's options. It adds only test routes (the test page, `/api/test/cs-timing`) and a larger buffer for the API's answers. D46 exists only on `release-1.1.2`.

The owner's ranges over the 3 runs:

| | The test build (1.1.1's page) | 1.1.2 |
|---|---|---|
| During the loss | the page stalled | the page stalled |
| A: until the uptime counts again | 10–40 s | 10–40 s |
| B: the reload after A | **30 s – 2 min** | **at once** |
| The captive-portal window | not seen | not seen |

**Reading [inf]:**
- **B is the difference D46 was for:** on 1.1.1's page, the requests it gave up on leave connections stuck on the Pico. A reload needs several connections and the page's many segments, so it waited for them. On 1.1.2 the reload went straight through.
- **A is the same range for different reasons:** a status request needs only one free connection, so 1.1.1's page got one through as soon as a stuck connection cleared. On 1.1.2 it's the back-off's wait: up to 30 s, plus the 8 s time-out.
- **Runs 2 and 3 started without a power cycle,** on both firmwares, so they may have started with connections left from the run before.

## Other observations

- **"main() to PIO ready" is 592–609 µs on both consoles.** Payload 1 holds a payload on both, so D17's fingerprint runs first, as on 1.0.3 and 1.1.0 (596–613 µs). The firmware's code is the same as 1.1.1's: only the page changed.
- **The dev console read CS high at "PIO ready" on boots 2 and 3,** on 1.1.1, and low on boots 5–10 on 1.1.2. Every earlier boot of this console read low ([cs-at-start-up.md](cs-at-start-up.md)). It was off for several seconds before each, as usual (the owner). Not explained:
  - the firmware is the same, so it isn't 1.1.2;
  - the margins were 158.4 and 161.0 ms, within this console's range, so the Pico wasn't late enough to meet the console's first rise of CS, about 40 ms after "PIO ready";
  - it changes nothing for the injection, which counts only clocked edges (D36), and raw equalled clocked;
  - whether a warmed-up console explains it (the owner's question) has a test in [cs-at-start-up.md](cs-at-start-up.md) §7.
- **The main console read CS high on every boot,** as before.

## Margin

| Console | Firmware | Payload | Boots | Margin (ms) |
|---|---|---|---|---|
| Dev | 1.1.1 | Swiss r2174 | 2, 3 | 158.4, 161.0 |
| Dev | **1.1.2** | **the built-in** | **5–7** | **162.8–164.0** |
| Dev | **1.1.2** | **Swiss r2174** | **8–10** | **162.8–164.3** |
| Main | **1.1.2** | **the built-in** | **2–4** | **155.0–155.1** |
| Main | **1.1.2** | **Swiss r2174** | **5–7** | **155.2** |

The margins are the raw counter's; the clocked counter's are 2–3 µs later on every boot.

**Reading:** both consoles keep their margins: the dev console's within its 158–168 ms, the main console's within the 155–157 ms it has had with the other Pico ([cs-at-start-up.md](cs-at-start-up.md)).
