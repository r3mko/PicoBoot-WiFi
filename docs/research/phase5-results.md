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

## 5.4 Acceptance, first run (2026-11-11)

Build `1.0.0` (`25da3ce`). The owner got through part 1 (the upgrade from PicoBoot-DX) and steps 2.1–2.2 of the fresh install; step 2.3's upload hung.

**Finding: clients lost their address every 10 minutes.** The upload of Swiss on the Payloads tab hung, and the Mac showed the captive-portal page again after 10 minutes on the access point. The DHCP server (from pico-examples) answered a request only if it named the address in option 50, as a client does when it joins. A client renewing its lease names it in `ciaddr` instead (RFC 2131 4.3.2), and those requests went unanswered. Before the security review that didn't matter, because a lease lasted a day. With step 5.1's 10-minute leases (L4), a client renews after 5 minutes, isn't answered, and loses its address at 10 minutes. It then joins again, but an upload running at that moment stalls, which is most likely what happened in step 2.3. The server now takes the address from `ciaddr` and answers to it; a host test (`tests/host/test_dhcpserver.c`) covers joining, renewing and the leases running out, and fails on the old code.

## 5.4 Acceptance, second run (2026-11-11)

Build `1.0.0` (`9a3b17b`, with the DHCP fix). The upload of step 2.3 hung again, and the Pico restarted.

**Finding: an upload or a delete hung the Pico.** Before a payload slot is erased, `invalidate()` programs its first page to zeros (phase 4 review, finding 2). The zeros came from a `const` array, which the linker puts in flash (`.rodata`, `0x1001fc5c`). Flash can't be read while it's being programmed, so `flash_range_program()` stalled at the upload's first sector, with interrupts off, until the 8 s watchdog reset the Pico. A delete runs the same code. Neither was run on the Pico after that review: the 4.9 and 5.1 re-tests had no payload upload or delete, and the host tests use simulated flash. The zeros are now a RAM buffer (`.uninitialized_data`, so `.bss` is unchanged), and `storage.h` says the data must be in RAM. Every other flash write already programs from RAM: the record and log writers, and the two upload writers.
