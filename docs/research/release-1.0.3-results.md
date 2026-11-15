# Release 1.0.3 results

The focused re-test of release 1.0.3 (the plan's step R4), run by the owner on 2026-11-15.

**Status:** R4 passed. For the owner's approval with plan v1.15.

**Setup:** the Pico 2 W from phases 0 to 5 (SSID suffix C2C4, RP2350 A2) in the DOL-001, on 3V3, with the SD2SP2 holding Swiss as `ipl.dol`. It ran 1.0.2 in slot A. Payload 1 held Swiss (1319 transfers) and payload 2 a payload of 307 transfers; the built-in gekkoboot was the choice. Boot numbers are the boot log's, which held boots 65–92 when it was read.

**Build:** `1.0.3` from `59cbc84`, the build after the code review (R3):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.0.3.uf2` | `4f59bc3cb93026e71a63821715ca7b933af01836aeea82e9769ffa13ee2caae4` |
| `picoboot-wifi-1.0.3-update.bin` | `684c8ef77ec89945e848556c661da5ded4f5dfa5ee225dfe85f97a1bceb73b65` |
| `picoboot-wifi.py` | `592ad48f7a9061a4b80cc939c1a20930feb26348334366ed75960413cfd28851`, the same as 1.0.0's |

## The checks

| Check | Boots | Result |
|---|---|---|
| The old built-in on 1.0.2, holding D-Pad Down | | **Passed:** the owner saw "gekkoboot undefined". That boot is older than the log: none of boots 65–82 on 1.0.2 chose the built-in |
| Update over Wi-Fi from 1.0.2 (the Firmware tab) | 83, 84 | **Passed:** into slot B, with Wi-Fi blob B, then its restart: a warm boot, "not run" (C2). Boot 84, a power-on, booted the console on trial and kept it: no trial from boot 85. The Firmware tab shows 1.0.3 in slot B, with no trial banner |
| Cold boots of the built-in gekkoboot | 84–92 | **Passed:** 9 power-ons, all complete, CS 285/285 (60 transfers), injection 19,421 µs each |
| The new built-in, holding D-Pad Down | one of 84–92 | **Passed:** the owner saw "gekkoboot r9.4" |
| Swiss from the SD2SP2 | | **Passed:** the built-in gekkoboot started `ipl.dol` |
| Every power-on | 84–92 | Complete, raw equal to clocked (no glitch), slot B as installed (no fallback) |
| The page | 83–92 | **Passed:** up; the boot log was read from it after boot 92 |

## What this shows

- **The built-in is the r9.4 release's build on the console.** Its screen reads "gekkoboot r9.4" where the old build's read "gekkoboot undefined". It loads Swiss from the SD2SP2 as before.
- **The injection is unchanged:** the image is the same size, so the console reads the same 60 transfers.
- **"main() to PIO ready"** is 596–599 µs. Swiss is in payload 1, so D17's fingerprint of it runs first; with payload 1 empty it was 389 µs ([release-1.0.2-results.md](release-1.0.2-results.md), boot 48). On 1.0.2, boots 65–82 took 605–611 µs with payload 1 or 2 chosen. The 10 µs difference probably comes from the choice, not from 1.0.3: a chosen payload 1 or 2 is checked first (`payload_check()` in `main.c`), and the built-in isn't [inf]. Nothing before "PIO ready" changed in 1.0.3, but no built-in boot on 1.0.2 in this log can confirm it.

## Margin

| Build | Payload | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|---|
| **`1.0.3`** | **The built-in gekkoboot (the r9.4 release's build)** | **84–92** | **165.4–166.0** | **165.6** |
| `1.0.2` | Swiss in payload 1, or 307 transfers in payload 2 (this log) | 65–82 | 162.4–165.5 | 164.3 |
| `1.0.2`, the built-in gekkoboot ([release-1.0.2-results.md](release-1.0.2-results.md)) | | 48 | 164.2 | |
| `1.0.1`, the built-in gekkoboot ([release-1.0.1-results.md](release-1.0.1-results.md)) | | 18–27 | 165.7–166.1 | 165.9 |
| PicoBoot-DX (phase 0) | | | | 165.5 |

**Reading:** the margins are 1.0.1's with a built-in of the same size, and within the console's spread seen before (160.0 ms once in phase 5, 166.1 ms on 1.0.1). The lowest, 165.4 ms, is 0.1 ms below PicoBoot-DX, well within D10's 10 ms.
