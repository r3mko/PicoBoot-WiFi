# Phase 4 results

Hardware tests from implementation plan phase 4, run by the owner on the `phase-4-payload-ota` builds.

**Status:** in progress (steps 4.1 to 4.7 checked on the bench and on phones; 4.8 console tests run once, the margin to re-measure).

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

## 4.8 Console tests, first run (2026-11-11)

**Setup:** the DOL-001 with the Pico on 3V3; build `e74bc99` over Wi-Fi into slot A on trial; the CLI and the page from the Mac. Boot numbers are the boot log's.

| Part | Boots | Result |
|---|---|---|
| 1: the new firmware on trial; Swiss from its UF2 (payload 1) | 13–15 | **Passed:** Swiss on B (13), the update's warm reboot into A (14), Swiss on A on trial, complete, kept by that power-on (15) |
| 2: gekkoboot from its RP2040-only UF2 (payload 2) | 16 | **Passed** |
| 3: the built-in gekkoboot | 17 | **Passed** |
| 4: gekkoboot as a `.dol`, converted and uploaded by the page (into payload 2, D42) | 18 | **Passed.** The page showed "NaN KB" for its size: fixed in `aa512e1` |
| 5: original IPL once | 19–20 | **Passed:** the console's own IPL (19, `skipped`), Wi-Fi up; payload 2 at the next power-on (20) |
| 6: the USB rule: an RP2350 UF2 of gekkoboot dragged onto the Pico, then the console's power-on | 21–23 | **Passed:** the drag's reboot on USB power (21, no console, notice); the console's power-on booted payload 1 and showed the notice (22, the `e74bc99` change); none at the next (23) |
| 7: a choice over Wi-Fi after a USB install (Swiss dragged), before the console's power-on | 24–26 | **Passed:** two starts on USB power (24, 25), the choice of payload 2, then payload 2 at the console's power-on, no notice (26) |
| 8: power cut at 53 % of a UF2 upload into payload 1 | 27 | **Passed:** payload 2 booted, no false USB switch; payload 1 empty |
| 9: payload 1 restored from the raw backup of step 4.4 | 28 | **Passed:** Swiss, complete; SHA-256 `1dc3e8e6…` |

**Swiss has no IPL `.dol`:** its release ships the IPL payload only in the PicoBoot UF2 (its `DOL/swiss_r2119.dol` is the regular build, refused by the converter). So Swiss was tested as a UF2 and as a raw payload, gekkoboot as a UF2 and as a `.dol`.

**Injection:** every console power-on complete; CS 1544/1544 for Swiss (406.8 ms), 285/285 for gekkoboot (19.4 ms).

| Boots | Margin (ms) |
|---|---|
| 13, 15, 16, 17, 18, 20 | 154.1, 157.0, 157.1, 155.3, 155.3, 156.3 |
| 22, 23, 26, 27, 28 | 158.0, 158.3, 158.2, 154.2, 157.3 |

**Finding: the margin.** 154.1–158.3 ms, mean 156.5 ms, against 158.5–161.6 ms (mean 159.4) in phase 3: 9.0 ms below PicoBoot-DX's 165.5 ms, close to D10's 10 ms. The Pico's own start-up to PIO ready ("main() to PIO ready") is 0.38–0.48 ms; what isn't measured is before `main()`: the SDK's start-up zeroes `.bss` on the clock the boot ROM left running (the loader keeps it), and phase 4's payload writer had added 8.8 KB to its 67 KB. The console was cold (the RP2350 read 10–15 °C), which phase 3 also saw lower margins with. `aa512e1` keeps the two upload writers and lwIP's pools out of `.bss` (77 KB → 20 KB); to be measured.
