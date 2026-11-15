# Release 1.0.2 results

The re-test of every write path for release 1.0.2 (the plan's step R4), run by the owner on 2026-11-15.

**Status:** R4 passed. Approved by the owner with plan v1.13; release 1.0.2 merged into `main` on 2026-11-15.

**Setup:** the Pico 2 W from phases 0 to 5 (SSID suffix C2C4, RP2350 A2) in the DOL-001, on 3V3, running 1.0.1 in slot B. Payload 2 held a payload of 1319 transfers, Swiss's size, and was the choice. Boot numbers are the boot log's.

**Build:** `1.0.2` from `405fc62`, the build after the code review (R3):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.0.2.uf2` | `2d79a4c6d4f0e0e2ca9fa90102f176089d4477d3fcb981ddaa4d44ad6f563639` |
| `picoboot-wifi-1.0.2-update.bin` | `fb92eb376ee099c32b0e54d34c9a813235fb539d9e5dd031523373c05f9d264d` |
| `picoboot-wifi.py` | `592ad48f7a9061a4b80cc939c1a20930feb26348334366ed75960413cfd28851`, the same as 1.0.0's |

## The checks

| Check | Boots | Result |
|---|---|---|
| Update over Wi-Fi from 1.0.1 (1.0.1's update writer) | 41 | **Passed:** into slot A. Boot 41, a power-on, keeps it, and 1.0.2 writes boot control |
| The same update again, from 1.0.2 (1.0.2's update writer; R3's finding 1) | 42, 43 | **Passed:** into slot B, then its restart: a warm boot, "not run" (C2). Boot 43, a power-on, keeps it. The Status tab shows 1.0.2 in slot B and in slot A, with no trial banner |
| A payload upload (the copy, the payload writer) | 43–44 | **Passed:** Swiss, from the page, into payload 1. No reset: no boot is logged between 43 and 44, and the uptime kept counting |
| 3 cold boots of that payload (the boot log) | 44–46 | **Passed:** all complete, CS 1544/1544 |
| A settings save (the record writer) | 47 | **Passed:** the Tx power from Low to Medium, still Medium after the cold boot, then back to Low |
| A payload delete (the copy again; R3's finding 2) | 47–48 | **Passed:** the built-in chosen first, then payload 1 deleted. The Payloads tab shows it empty, and still after boot 48. No reset: no boot is logged between 47 and 48, and the uptime kept counting |
| A cold boot of the built-in gekkoboot | 48 | **Passed:** complete, CS 285/285. "main() to PIO ready" is 389 µs, phase 5's 0.4 ms with payload 1 empty |
| Every power-on | 41, 43–48 | Complete, raw equal to clocked (no glitch), slot A and then B as installed (no fallback) |
| The page | 41–48 | **Passed:** up throughout; the boot log was read from it |

## What this shows

- **The copy works on the Pico.** The upload and the delete each program `invalidate()`'s zeros, which are now in flash (`.rodata`, `0x1001fd40`). They aren't read anywhere else, so they miss the XIP cache. Before the copy, that hung the Pico until the watchdog reset it (phase 5); now no reset is logged.
- **It can't show** that the zero page lands, because the erase follows at once. The second and later pages of the copy loop never run in 1.0.2, since the only copy is one page. The code review checked both by reading.
- **Every other writer** programs from RAM, exactly as in 1.0.1, and each ran: both update writers, the payload writer, the settings record, boot control and the boot log.

## Margin

| Build | Payload | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|---|
| **`1.0.2`** | **1319 transfers (payload 2 at 41 and 43; Swiss in payload 1 at 44–47)** | **41, 43–47** | **160.2–163.4** | **161.4** |
| **`1.0.2`** | **The built-in gekkoboot** | **48** | **164.2** | |
| `1.0.1`, the built-in gekkoboot ([release-1.0.1-results.md](release-1.0.1-results.md)) | | 18–27 | 165.7–166.1 | 165.9 |
| `1.0.0`, Swiss ([phase 5](phase5-results.md), third run) | | 3, 4, 8–13 | 164.7–165.4 | 165.2 |
| Phase 4's build, Swiss ([phase 4](phase4-results.md)) | | 45, 47–52 | 162.0–164.1 | 163.0 |
| PicoBoot-DX (phase 0) | | | | 165.5 |

**Reading:** the margins are about 1.7 ms below 1.0.1's and phase 4's, and 3.8 ms below phase 5's third run. Nothing in 1.0.2 runs before "PIO ready": every write comes after the injection (C6), and "main() to PIO ready" (389 µs at boot 48) is phase 5's. So the difference is the console's own spread; phase 5 saw 160.0 ms once (its boot 53). The lowest margin, 160.2 ms, is 5.3 ms below PicoBoot-DX, within D10's 10 ms.
