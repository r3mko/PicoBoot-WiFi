# Release 1.1.1 results

The console test of release 1.1.1 (the plan's step R4), run by the owner on 2026-11-20 on the second console.

**Status:** R4 passed. Approved by the owner with plan v1.19 and design v1.11; release 1.1.1 merged into `main` on 2026-11-20.

**Setup:**
- **The console:** the owner's second DOL-001, with a GC2SD in a memory-card slot and an HDMI adapter ([cs-pull-up.md](cs-pull-up.md)). The Pico's ground wire is the short one, to a ground point near U10.
- **The Pico:** `PicoBoot-AE52` (RP2350 A2), on 3V3. Before the test it ran the test build `+9a10a77` in slot B, from the research.
- **The payloads:** payload 2 held Swiss r2119's PicoBoot UF2 (an IPL build, 1319 transfers, so 1544 CS transfers expected). The built-in gekkoboot r9.4 is 60 transfers (285 expected).

Boot numbers are the boot log's. The owner read the log twice from the Diagnostics tab (boots 17–47, then 33–52); the numbers here come from those two reads.

**Build:** `1.1.1` from `b7dc50b`, the build after the code review (R3):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.1.1-update.bin` | `714140b8497cda5a097a37ee9ecca36d65d3ced828f170037045901624bcb151` |
| `picoboot-wifi-1.1.1.uf2` | `cfaed2930f9565df11099fbb37e6f8cba8ef23e0993582ab1eb4fe85b94db6dd`, from the same build. R4 didn't install it |
| `picoboot-wifi.py` | `6985a22237d7d624ff78bf558170406d6bf06598cc9fe21fc6f5ed2728209f9b`, unchanged since 1.1.0 |
| 1.1.0, for the first step | the release's `picoboot-wifi-1.1.0-update.bin`, `eb16d06f54ab8e39e8a33cab722939a987e00ad980e5607d92aa340e4e9b6e4b` |

## The checks

| Check | Boots | Result |
|---|---|---|
| Bit 7 cleared | before 30 | **Done:** "CS pull-up" on the test page, before leaving the test build (the owner). The dev console's Pico was set the same way after the test |
| 1.1.0 over Wi-Fi, from the test build | 30 | **Passed:** into slot A, then its restart: a warm boot, "not run", on trial |
| The update over Wi-Fi from 1.1.0 | 31, 32 | **Passed:** into slot B while 1.1.0 was still on trial (no power-on between), then its restart, "not run". Boot 32, a power-on, booted the console on trial and kept 1.1.1 |
| Swiss's IPL build | 32–47 | **Passed:** 16 cold boots, each starting Swiss (the owner); 1544 raw and clocked every time |
| The built-in gekkoboot | 48–52 | **Passed:** 5 cold boots, each starting gekkoboot (the owner); 285 raw and clocked every time |
| Every power-on | 32–52 | Complete, raw equal to clocked (no glitch), CS high at "PIO ready", slot B (no fallback) |
| The boot log across the format change | 17–29 | **Passed:** the test build's entries, which still carry `cs_pulldown_test`, read in 1.1.1 with the counts [cs-pull-up.md](cs-pull-up.md) §2 has for boots 17–28 |
| The page | 32–52 | **Passed:** the Firmware tab shows 1.1.1 with no trial banner, and the Diagnostics tab shows the log (the owner) |

## The last boot on the test build

Boot 29, on `+9a10a77` [inf: in pull-up mode, as boots 23–28], showed the console's own menu (the owner).
- Its CS counters read 635 raw and 635 clocked, 350 more than expected.
- Its first transfer came 157.6 ms after "PIO ready", 1.3–1.8 ms earlier than on boots 17–28 (159.0–159.4 ms).

The pull-up's other failures had one to four extra transfers. Why this one had 350 isn't known. It's one more failure with the pull-up, which 1.1.1 removes.

## What this shows

- **Without the pull-up, the second console booted every time:** 21 of 21 cold boots on 1.1.1, after 14 of 14 in the test builds' pull-down mode. With the pull-up, on the same Pico and wires, it booted 2 of 7 (boots 23–29).
- **No glitch on this console:** CS reads high at "PIO ready" on every boot and raw equals clocked, so the false edge that the clock-qualified count guards against doesn't come here. The dev console showed the clock check at work ([cs-pull-up.md](cs-pull-up.md) §3).
- **The boot log kept its format,** as the code review found for the Pico's build: entries the test build wrote read correctly in 1.1.1.
- **An update from 1.1.0 still on trial works:** the update came before 1.1.0's first power-on.
- **Nothing before "PIO ready" changed:** `main()` to PIO ready is 382–386 µs with the built-in, against 387–390 µs on the test build's boots 17–29, and 394–396 µs with Swiss in payload 2.

## Margin

| Build | Payload | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|---|
| **`1.1.1`** | **Swiss's IPL build** | **32–47** | **159.4–159.9** | **159.7** |
| **`1.1.1`** | **the built-in gekkoboot** | **48–52** | **159.7–160.1** | **159.9** |
| **`1.1.1`, all** | | **32–52** | **159.4–160.1** | **159.7** |
| `+9a10a77`, the same log | the built-in | 17–28 | 159.0–159.4 | 159.2 |

The margins are the raw counter's; the clocked counter's are 2–3 µs later on every boot.

**Reading:** this console's margin is 159–160 ms on both builds, within the dev console's 158.2–164.8 ms. 1.1.1 changes nothing on the boot path's timing, and its margin matches the test build's in the same log.
