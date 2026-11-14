# Release 1.0.1 results

The focused re-test of release 1.0.1 (the plan's step R4), run by the owner on 2026-11-14.

**Status:** R4 passed.

**Setup:** the Pico 2 W from phases 0 to 5 (SSID suffix C2C4, RP2350 A2) in the DOL-001, on 3V3, running 1.0.0 in slot A. No payloads stored, so every boot sends the built-in gekkoboot (60 transfers). Boot numbers are the boot log's.

**Build:** `1.0.1` from `f6aa5e8`, the build after the code review (R3):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.0.1.uf2` | `29d26b8aa84c232136055172e7ea53bd52cd0e8ce3101ce15e13c88f30eb5580` |
| `picoboot-wifi-1.0.1-update.bin` | `52ade2f9bdba34dff0dd3569e0b1cb3fd5372b9bb2d8e89da1a572790189add2` |
| `picoboot-wifi.py` | `592ad48f7a9061a4b80cc939c1a20930feb26348334366ed75960413cfd28851`, the same as 1.0.0's |

## The checks

| Check | Boots | Result |
|---|---|---|
| Update over Wi-Fi from 1.0.0 | 17 | **Passed:** into slot B, then its restart: a warm boot, "not run" (C2) |
| 10 cold console boots | 18–27 | **Passed:** all complete, CS 285/285 each, slot B throughout (no fallback) |
| The trial kept (D12) | 18 | **Passed:** the Firmware tab shows 1.0.1 in slot B kept, no trial banner |
| On USB power without the console | 28 | **Passed:** "no console seen", CS 0/0 (the incomplete path through `injection_finish()`); Wi-Fi and the page up (the boot log was read from it) |
| The page | 17–28 | **Passed:** up after every boot read |
| DATA's idle voltage (optional) | | Not measured |

## Margin

| Build | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|
| **`1.0.1`, the built-in gekkoboot** | **18–27 (10 power-ons)** | **165.7–166.1** | **165.9** |
| `1.0.0`, the built-in gekkoboot ([phase 5](phase5-results.md)) | 2 | 166.0 | |
| `1.0.0`, Swiss (phase 5, third run) | 3, 4, 8–13 | 164.7–165.4 | 165.2 |
| PicoBoot-DX (phase 0) | | | 165.5 |

**Reading:** the margin and the CS counts are 1.0.0's, as expected: every change of 1.0.1 runs after the console has its payload. The boots used the built-in gekkoboot only, as no payload was stored; a payload from flash takes the same path through `injection_finish()` once its DMA has ended.
