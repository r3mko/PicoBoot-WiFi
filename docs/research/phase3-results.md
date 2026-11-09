# Phase 3 results

Hardware tests from implementation plan phase 3, run by the owner on the `phase-3-firmware-ota` builds.

**Setup:** the Pico 2 W from phases 0 to 2 (SSID suffix C2C4, RP2350 A2); `curl` from a Mac joined to the access point.

## Bench check of step 3.3 (2026-11-09, USB power, no console)

Build `daeff5d`, installed over USB into slot A; its update file `picoboot-wifi-update.bin` (420,808 bytes, with the Wi-Fi blob).

| Check | Result |
|---|---|
| Upload the update file | **Passed:** `{"installed":true,"slot":"B","version":"0.3.0-dev+daeff5d","blob_written":true}` in **4.4 s**; the test page shows slot B on trial and pending |
| The same file again | **Passed:** `"blob_written":false` (Wi-Fi B already held it), 4.0 s |
| A copy with one bit flipped in the image | **Passed:** `bad_image`; slot B shows as empty afterwards |
| The USB install UF2 uploaded by mistake | **Passed:** `not_an_update`, "this is the USB install file" |
| The update sent as `application/json` | **Passed:** `unsupported_type` |
| Reboot | **Passed:** back on slot A (the loader doesn't start a pending slot until step 3.4) |

**Finding:** an upload takes about 4 seconds, less than the 10 the API description estimated; most of it is erasing flash. `docs/api.md` now says "a few seconds".

## Bench check of steps 3.4 and 3.7 (2026-11-09, USB power, no console)

Build `6b8b794` over USB (loader version 2, slot A), then its update files: the normal one, and the two crash builds of step 3.7.

| Check | Result |
|---|---|
| The loader reports version 2 | **Passed** |
| A good update: installed into B, reboots by itself into B on trial | **Passed:** `"rebooting":true`; the test page shows slot B on trial and pending |
| A reboot from the page during the trial | **Passed:** back on B, still on trial (a reboot the firmware means isn't a crash) |
| An update while on trial | **Passed:** 409 `on_trial`; slot A, the fallback, isn't touched |
| The start-up crash build (`-crash-start`) on trial | **Passed:** B faults at once; the loader goes back to A, which records B as failed; after a power cycle still A, B still failed |
| The hang build (`-crash-5s`) on trial | **Passed:** B runs on trial, hangs after 5 s, the watchdog resets it, the loader goes back to A, B failed |
| The damaged file | **Passed:** `bad_image` |

**Not tested on the bench:** keeping a trial (it takes a complete power-on injection) and the crash loop, which needs a kept slot. Both are in the console tests of step 3.8.

## 3.8 Console tests, first run (2026-11-10)

**Setup:** the DOL-001 with the Pico on 3V3, Swiss r2119 in payload 1; build `d97ac6a` over USB, then its update files from the Mac (`tools/picoboot-wifi.py`) and the phone (the Firmware tab).

| Part | Result |
|---|---|
| Bench: the Firmware tab uploads with progress, the trial banner, Keep, the CLI's `status` and `boot` | **Passed** |
| 1: update into B, kept by the next power-on | **Passed:** Swiss started; B kept, A kept as the slot it replaced |
| 2: switching slots, a power-on on each | **Passed** |
| 3: power cut at 57% of an upload | **Passed:** Swiss started from B; slot A empty |
| 4: the start-up crash build on trial | **Passed:** back on B, A failed |
| 5: the crash loop | **Not tested: a bug.** The hang build was never started (see below) |
| 6: an update with another Wi-Fi blob | **Failed its trial:** the SDK's Bluetooth blob, the planned stand-in, didn't come up (see below) |
| Injection | Every power-on complete, margin 158.6–160.0 ms, CS 1544/1544 |

**Bug (part 5):**
- After part 4's failed trial of slot A, the loader's session word still noted "A's trial failed", and it never tries a failed trial again before the next power-on, so that a crash can't loop it.
- But that note also blocked the new upload into A in the same session: the loader went straight back to B (boot 20). The end state looked like a pass: B running, A failed.
- The retries in part 6 (boots 25 and 26) were blocked the same way.
- **Fix (`05b5366`):** the firmware clears the note when it sets up a new trial of the slot: an upload, or booting a failed slot again. Host test of this sequence.

**Part 6:**
- The `-btblob` build in A started (boot 23 logged), then didn't come up, and the watchdog reset it; the loader went back to B, which marked A failed. That's D12 working: a new firmware that doesn't work is undone on its own.
- The update itself worked: it wrote the new blob into Wi-Fi A and checked it.
- The likely cause is the blob: the SDK's Bluetooth variant needs the Bluetooth side set up, which this Wi-Fi-only build doesn't do.
- The stand-in is now the same Wi-Fi blob with a marker in its spare header bytes (`PICOBOOT_WIFI_BLOB=marked`): a blob that works but differs.
