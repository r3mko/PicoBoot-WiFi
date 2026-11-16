# Release 1.1.0 results

The console test of release 1.1.0 (the plan's step R5), run by the owner on 2026-11-16.

**Status:** R5 passed. For the owner's approval with plan v1.17 and design v1.9.

**Setup:**
- **The Pico:** the Pico 2 W from phases 0 to 5 (SSID suffix C2C4, RP2350 A2) in the DOL-001, on 3V3, running 1.0.3 in slot A.
- **The SD2SP2's card:** set up as cubiboot's own "Method 1": `ipl.dol` is cubiboot v1.13.0 and `swiss-gc.dol` is Swiss r2119, with cubiboot's `config.ini`.
- **The payloads:** payload 1 held Swiss r2119's PicoBoot UF2 (an IPL build, 1319 transfers). Each ordinary `.dol` went into payload 2 in turn.

Boot numbers are the boot log's.

**Build:** `1.1.0` from `3713e8d`, the build after the code review (R4):

| File | SHA-256 |
|---|---|
| `picoboot-wifi-1.1.0.uf2` | `1940e336d238bbd7f432084615116ec52af0aa47114d08dd284e96a6f12b0fdf` |
| `picoboot-wifi-1.1.0-update.bin` | `eb16d06f54ab8e39e8a33cab722939a987e00ad980e5607d92aa340e4e9b6e4b` |
| `picoboot-wifi.py` | `0fc57c9ae320a6ab032e07432e71e07215b473019fb1c603d684afd7520bf9b2` as tested. The release's is `6985a22237d7d624ff78bf558170406d6bf06598cc9fe21fc6f5ed2728209f9b` (`79f6879`), which changes only the wording of the payload line, after R5 |

**The `.dol` files:**

| `.dol` | What it is | As a payload |
|---|---|---|
| cubiboot v1.13.0 `ipl.dol` | libogc2, linked at `0x80003100`, with a 2.5 MB BSS | 733,508 B, 717 transfers |
| gekkoboot r9.4, lowmem build | libogc2, packed, linked at `0x80022660`. It's the `.dol` inside the release's `gekkoboot_memcard.gci`, after the 64-byte GCI header | 61,188 B, 60 transfers |
| Swiss r2119 `DOL/swiss_r2119.dol` | Swiss's packer, which unpacks Swiss itself | 1,350,596 B, 1319 transfers |

The plan's third file was GCMM or the 240p Test Suite, but neither fits a slot. GCMM 1.5.2's GameCube `.dol` is 3.8 MB, and the 240p Test Suite's GameCube download is 2.8 MB zipped. gekkoboot's lowmem build replaced it, the owner's choice.

## The checks

| Check | Boots | Result |
|---|---|---|
| Update over Wi-Fi from 1.0.3 | 128, 129 | **Passed:** into slot B, then its restart: a warm boot, "not run" (C2). Boot 129, a power-on, booted the console on trial and kept it. The Firmware tab shows 1.1.0 in slot B, with no trial banner |
| cubiboot's `ipl.dol`, from the page | 130–133 | **Passed:** 3 cold boots after a long power-off, then a quick power cycle. Each time, cubiboot's menu, then Swiss from `swiss-gc.dol` |
| gekkoboot's lowmem `.dol`, from the page | 134–137 | **Passed:** the same boots. "gekkoboot r9.4" with D-Pad Down; without it, `ipl.dol` from the card (cubiboot) |
| Swiss's `.dol`, from the page | 138–141 | **Passed:** the same boots. Swiss starts and shows the SD card |
| The page's banner | | **Passed:** "An ordinary .dol: a small loader in front of it puts it in place at power-on." for each |
| An IPL build still boots | 142 | **Passed:** Swiss's UF2 in payload 1 |
| cubiboot's `ipl.dol`, from the CLI | after 142 | **Passed:** "converted with a loader stub". The Pico reports SHA-256 `a54c20f4cf8742cc373d25549bc23b75d4a63553fc6d4e73d3454ff846a33ecd`, which is the CLI's conversion and the same bytes the page makes (the tests). A cold boot: cubiboot's menu, then Swiss. That boot came after the log was read |
| Every power-on | 129–142 | Complete, raw equal to clocked (no glitch), slot B (no fallback) |
| The page | 128–142 | **Passed:** up; the boot log was read from it |

## What this shows

- **The stub works on the console.** Three `.dol` files of two kinds boot from a payload slot, each after a long power-off: two libogc2 programs, and Swiss's packer. Every boot runs all of the stub's steps: real mode, the hardware quiesced, low memory and the caches.
- **cubiboot was where D43's research started.** Its own PicoBoot stub has no cache handling and fills no low memory, and it fails after a long power-off on this console ([dol-loader.md](dol-loader.md) §6). The same program behind D43's stub booted on 3 cold boots from the page and 1 from the CLI. That shows D43's stub doesn't have the failure; it doesn't prove what causes cubiboot's own [inf].
- **The memory size works** (the code review's finding 2). The stub reads it from the memory controller as one 32-bit word. cubiboot and Swiss size their memory from `0x28`, and both ran normally; a wrong size would most likely have crashed them [inf].
- **Nothing before "PIO ready" changed:** `main()` to PIO ready is 605–613 µs with Swiss in payload 1, as on 1.0.3.

## Margin

| Build | Payload | Boots | Margin (ms) | Mean (ms) |
|---|---|---|---|---|
| **`1.1.0`** | **cubiboot with the stub** | **130–133** | **158.2–163.1** | **160.5** |
| **`1.1.0`** | **gekkoboot lowmem with the stub** | **134–137** | **158.5–163.8** | **160.7** |
| **`1.1.0`** | **Swiss's `.dol` with the stub** | **138–141** | **162.8–163.7** | **163.1** |
| **`1.1.0`** | **Swiss's UF2** | **129, 142** | **160.7–161.4** | **161.0** |
| `1.1.0`, all | | 129–142 | 158.2–163.8 | 161.4 |
| `1.0.3`, the same log | the built-in gekkoboot, Swiss, cubiboot's UF2 | 113–127 | 158.7–164.8 | 162.6 |
| PicoBoot-DX (phase 0) | | | | 165.5 |

**Reading:** the lowest margin, 158.2 ms, is the lowest yet. That is 7.3 ms below PicoBoot-DX, within D10's 10 ms. The stub runs only after the injection, so it can't change the margin, and 1.0.3 reached 158.7 ms in the same log. This is the console's spread.
