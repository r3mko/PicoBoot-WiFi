# Phase 5 results

Hardware tests from implementation plan phase 5, run by the owner on the `phase-5-release` builds.

**Status:** step 5.1's re-test passed, and the acceptance (step 5.4) passed on its third run, build `1.0.0` (`df962b8`). The first two runs found two bugs, both fixed. The code review's fixes passed their re-test (`9b8a107`), and so did the release build, `1.0.0` (`c468ee9`), with the device's new name on the page.

**Setup:** the Pico 2 W from phases 0 to 4 (SSID suffix C2C4, RP2350 A2) in the DOL-001, on 3V3. The CLI and the page run on a Mac, an iPhone and an Android phone joined to the access point. In step 5.1 payload 1 holds Swiss r2119; the acceptance erases the flash first.

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

## 5.4 Acceptance, third run (2026-11-11)

Build `1.0.0` (`df962b8`, with both fixes). Boot numbers are the boot log's, which starts again after part 2's flash erase.

| Part | Boots | Result |
|---|---|---|
| 1: the upgrade from PicoBoot-DX, back to DX and to PicoBoot-WiFi again | | **Passed** (the owner). Part 2's flash erase cleared its boot log |
| 2: fresh install (`flash_nuke.uf2`, then `picoboot-wifi-1.0.0.uf2`) | 1–2 | **Passed:** the install's restart on USB power (1, `no_cs`: no console), then the built-in gekkoboot at the console's power-on (2, boot control's defaults) |
| 2: Swiss uploaded on the Payloads tab, into payload 2 | 3–4 | **Passed:** Swiss from payload 2, complete |
| 2: gekkoboot's `.dol` uploaded on the page (into payload 1, now the choice), payload 2 chosen again, payload 1 deleted | | **Passed** (the owner); Swiss from payload 2 at the power-ons after it |
| 3: the `0.4.0-dev+96f3309` update over Wi-Fi into slot B | 5–6 | **Passed:** the update's restart (5), then on trial, complete (6) |
| 3: the `1.0.0` update over Wi-Fi into slot A | 7–8 | **Passed:** the update's restart (7), then on trial, complete, kept (8) |
| 3: power-ons on 1.0.0 | 9–13 | **Passed:** all complete |
| 3: the password set in part 2, after both updates | | **Passed** |
| The Mac joined for 14 minutes (the DHCP fix) | | **Passed:** still connected; before the fix it lost its address at 10 minutes |

**Injection:** complete on every console power-on. CS 1544/1544 for Swiss (406.8 ms), 285/285 for the built-in gekkoboot (19.4 ms). Wi-Fi was up 785 ms after the start (boot 13).

| Build | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|
| **`1.0.0`** | **3, 4, 8–13 (8 power-ons)** | **164.7–165.4** | **165.2** |
| `1.0.0`, the built-in gekkoboot | 2 | 166.0 | |
| `0.4.0-dev+96f3309` | 6 | 164.8 | |
| `0.5.0-dev+3806ed3` (5.1 re-test) | 55, 57–62 | 160.3–163.0 | 161.6 |
| PicoBoot-DX (phase 0) | | | 165.5 |

**Margin:** 0.3 ms below PicoBoot-DX, and higher than in the 5.1 re-test. Part of that is known:
- Payload 1 is empty, so the start-up skips its fingerprint (D17): "main() to PIO ready" is 0.4 ms, against 0.6 ms with Swiss in payload 1.
- A release build zeroes 2 KB less `.bss` (18.2 KB), about 0.3 ms.

The `0.4.0-dev` build also read 164.8 ms in this session (boot 6), against 162.7 ms in phase 4, so the rest is most likely the console's spread from one session to the next.

## Re-test after the code review (2026-11-11)

Build `1.0.0` (`9b8a107`: finding 2 of the code review, see the implementation plan), installed over Wi-Fi from the third run's build. The CLI from `a5cb225` (finding 1).

| Check | Boots | Result |
|---|---|---|
| The update over Wi-Fi into slot B (the file `67b9da9d…`), the same version already running | 14–15 | **Passed:** the update's restart (14), then on trial, complete, kept (15) |
| An upload cut off at 43 % (Ctrl-C), then `GET /.api` | | **Passed:** `302 Found`, not the upload's "incomplete" answer (finding 2) |
| The same upload to the end | | **Passed:** into payload 1 as "Swiss r2119", SHA-256 `1dc3e8e6…`, now the choice |
| `ipl-once`, then `ipl-once --off` | | **Passed:** both answers |
| Power-ons with Swiss from payload 1 | 16–20 | **Passed:** all complete |

**Injection:** complete on every power-on, CS 1544/1544. Wi-Fi was up 785 ms after the start (boot 20).

| Build | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|
| **`1.0.0` (`9b8a107`)** | **15–20 (6 power-ons)** | **163.2–165.5** | **164.9** |
| `1.0.0` (`df962b8`, third run) | 3, 4, 8–13 | 164.7–165.4 | 165.2 |
| PicoBoot-DX (phase 0) | | | 165.5 |

**Result:** passed; 0.6 ms below PicoBoot-DX. With Swiss in payload 1 again, "main() to PIO ready" is back to 0.6 ms (boots 16–20), against 0.4 ms with payload 1 empty (boot 15). That's the fingerprint of the third run's note.

## Re-test after the rename (2026-11-11)

Build `1.0.0` (`c468ee9`): the page, the firmware's messages and the README call the device PicoBoot-WiFi (the owner's choice in step 5.2). It was installed over Wi-Fi into slot A from the previous build.

| Check | Result |
|---|---|
| The update over Wi-Fi into slot A, kept | **Passed** |
| The page's title and heading say PicoBoot-WiFi | **Passed** |
| A reboot | **Passed** |

**Result:** passed. This is the release build:

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.0.0.uf2` | `b7b5cfb8219ed8834f54213a2a6302cfa6ed7538b68694ebc1afeca818c976e0` |
| `picoboot-wifi-1.0.0-update.bin` | `51a5f9f3794fb251387d0b397f830138b6cd5488bb1ce79f01ee33a658368156` |
| `picoboot-wifi.py` | `592ad48f7a9061a4b80cc939c1a20930feb26348334366ed75960413cfd28851` |
