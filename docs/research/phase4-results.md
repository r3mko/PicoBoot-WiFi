# Phase 4 results

Hardware tests from implementation plan phase 4, run by the owner on the `phase-4-payload-ota` builds.

**Status:** in progress (steps 4.1 to 4.7 checked on the bench and on phones; the console tests of 4.8 to come).

**Setup:** the Pico 2 W from phases 0 to 3 (SSID suffix C2C4, RP2350 A2); `curl` and `tools/picoboot-wifi.py` from a Mac joined to the access point. The payloads are the release files Swiss r2119 (`PicoBoot/swiss_r2119.uf2`, RP2040 and RP2350 blocks) and gekkoboot r9.4 (`gekkoboot_pico.uf2`, RP2040 blocks only).

## Bench check of steps 4.1 to 4.3 (2026-11-10, USB power, no console)

Build `bc77912` over USB: loader version 3 and boot control format 2 (D42).

| Check | Result |
|---|---|
| The loader reports version 3; the test page has a Payloads line | **Passed** |
| Upload Swiss, named "Swiss r2119" | **Passed:** into payload 2 (payload 1 was the choice), now the choice; 1,349,956 bytes, SHA-256 `1dc3e8e6…`, the same as the host test's |
| Upload gekkoboot (RP2040-only UF2), named "gekkoboot r9.4" | **Passed:** into payload 1, now the choice; 60,548 bytes, SHA-256 `0614b3f7…`, the same as the host test's |
| Choose payload 2; rename payload 1 | **Passed** |
| Delete payload 2, the choice | **Passed:** 409 `chosen` |
| Delete payload 1 | **Passed:** the test page shows it as none |
| The USB install file as a payload | **Passed:** `not_a_payload` |
| The USB rule (D17): Swiss dragged onto the Pico over USB | **Passed:** that power-on booted payload 1, marked as a USB install; payload 1 is the choice, with no name |
| Original IPL once | **Passed:** the next power-on skipped the injection (`IPL once` in the boot log), the one after was back to normal |

**Finding:** the test page's temperature read 407.7 °C. The include added for the Payloads line brought in the SDK's `platform_defs.h` before `pico.h`, so `page.c` took the RP2350B's ADC channel count and read an input this chip doesn't have. Fixed in `9a32032` (also in three other files with the same order, a static assert in `page.c`); the owner confirmed the temperature reads normally again.

## Bench check of step 4.4 (2026-11-10, USB power, no console)

Build `eb21cfb`, installed over Wi-Fi with the CLI and kept. Payload 1 holds Swiss from the USB install (no name), payload 2 Swiss as uploaded ("Swiss r2119").

| Check | Result |
|---|---|
| Payload backups | **Passed:** `payload1.bin` and `Swiss r2119.bin`, 1,349,956 bytes each, in 2.4 and 3.5 s; both SHA-256s are the status's |
| Firmware backup | **Passed:** 439,504 bytes in 1.6 s, byte-identical to the update file just installed |
| Settings backup, and restoring it | **Passed:** `{"ssid":"","country":"XX","channel":6,"tx_power":"medium","auto_off_minutes":10,"led":true}`; posted back: saved, no Wi-Fi restart |
| During a slowed-down download (`curl --limit-rate 100k`): delete payload 1, a second download, a payload upload | **Passed:** each 503 `busy`; nothing changed |
| The test page's boot log for the IPL-once boot | **Passed:** `-` for the payload, transfers and CS |

**Finding:** downloads run at about 0.3 to 0.6 MB/s, a few seconds for a payload. The busy answer of the delete named the field `slot`; it now names a field only when the field is the cause.

## Bench check of steps 4.5 to 4.7 (2026-11-11, USB power, no console)

Build `e1a2ad5`, installed over Wi-Fi with the CLI and kept (slot B). The web UI on the Mac, then the CLI.

| Check | Result |
|---|---|
| The page: `gekkoboot_r9_4.dol` converted in the browser and uploaded | **Passed:** into payload 2 as "Gekkoboot r9 4", the name the page made from the file name |
| The page: delete payload 1, upload `swiss_r2119.uf2` into it | **Passed:** payload 1 holds "Swiss r2119" |
| The page's other parts on the Mac, an iPhone and an Android phone: the tab bar, the Payloads list, upload from the phones' file pickers, choose (the built-in too), rename, delete, download, original IPL once, the firmware and settings backups, the settings restore | **Passed** (the owner) |
| CLI `status` | **Passed:** the payloads, their names and sizes, the next upload's slot, original IPL once |
| CLI `payload convert` of Swiss's regular `.dol` | **Passed:** refused, "Invalid entry point or base address: 0x80397940 / 0x00397940", as `process_ipl.py` says |
| CLI `payload upload gekkoboot_pico.uf2` | **Passed:** asked before replacing payload 2's "Gekkoboot r9 4"; installed as "Gekkoboot pico", SHA-256 `0614b3f7…` |
| CLI `payload choose`, `ipl-once`, `ipl-once --off` | **Passed:** the status shows each |
| CLI `backup payload2`, `firmware`, `settings` | **Passed:** saved under the Pico's file names (`Gekkoboot pico.bin`, `picoboot-wifi-0.4.0-dev+e1a2ad5-update.bin`, `picoboot-wifi-settings.json`), each checked first |
| CLI `restore-settings` | **Passed:** saved, no Wi-Fi restart |

**Finding:** the CLI named slots as the API does in two places ("payload1 chosen"); it now says "payload 1", like the rest of its output.
