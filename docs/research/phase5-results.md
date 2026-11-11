# Phase 5 results

Hardware tests from implementation plan phase 5, run by the owner on the `phase-5-release` builds.

**Status:** in progress. Step 5.1's re-test passed; the acceptance (step 5.4) is to come.

**Setup:** the Pico 2 W from phases 0 to 4 (SSID suffix C2C4, RP2350 A2) in the DOL-001, on 3V3. The CLI and the page run on a Mac, an iPhone and an Android phone joined to the access point. Payload 1 holds Swiss r2119.

## 5.1 Re-test of the security fixes (2026-11-11, in the console)

Build `0.5.0-dev+3806ed3` (the fixes of `360d8dd`), installed over Wi-Fi from `0.4.0-dev+96f3309`. Boot numbers are the boot log's.

| Check | Result |
|---|---|
| The update over Wi-Fi into slot B, written by the old firmware, kept by its first power-on | **Passed:** boot 54 (the update's warm reboot), then boot 55 (on trial, complete, kept) |
| The same update again, into slot A. This time the new firmware writes it, programming the first sectors of the Wi-Fi area and of the image last (I3) | **Passed:** the CLI asked first ("already running"); boot 56, then boot 57 (on trial, complete, kept). Both slots hold `0.5.0-dev+3806ed3` |
| The Mac, the iPhone and the Android phone join with 10-minute DHCP leases (L3, L4), and open the page with its new headers (L7) | **Passed** |
| A settings file that sets a password (L5) | **Passed:** refused by the CLI ("sets a password, which a PicoBoot backup never does") and by the page |
| CLI `backup payload1` in an empty folder (L6) | **Passed:** saved as `Swiss r2119.bin`, 1,349,956 bytes, SHA-256 `1dc3e8e6…` |

**Injection:** complete on every power-on, CS 1544/1544 (406.8 ms). Wi-Fi was up 788 ms after the start (boot 62).

| Build | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|
| `0.4.0-dev+96f3309` (phase 4 re-test) | 45, 47–53 (8 power-ons) | 160.0–164.1 | 162.7 |
| **`0.5.0-dev+3806ed3`** | **55, 57–62 (7 power-ons)** | **160.3–163.0** | **161.6** |
| PicoBoot-DX (phase 0) | | | 165.5 |

**Result:** passed. The mean margin is 1.1 ms lower, within the console's spread: the old build's last power-on (boot 53) was 160.0 ms. "main() to PIO ready" is 600 µs, against 593 µs before, and `.bss` is 20.2 KB, within the 24 KB budget. The margin is 3.9 ms below PicoBoot-DX, within D10's 10 ms.
