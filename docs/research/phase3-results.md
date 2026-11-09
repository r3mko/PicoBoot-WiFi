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
