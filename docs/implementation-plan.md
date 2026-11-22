# PicoBoot-WiFi: implementation plan

| | |
|---|---|
| **Status** | Approved v1.22, 2026-11-22 |
| **Design** | [architecture.md](architecture.md), Approved v1.15 |
| **Owner** | Remko Kleinjan |

## Status

| Version | Date | What changed and why |
|---|---|---|
| v0.1 draft | 2026-11-08 | First draft. It splits the design's phases (D28) into steps, assigns models and lists what's needed from the owner. The owner's equipment (a current meter, no oscilloscope or logic analyzer) and console (DOL-001 only) don't fit the design's scope-based measurements. Design points DP1–DP5 propose the changes. |
| v1.0 | 2026-11-08 | The owner approved the draft with DP1–DP5 ("approved, go ahead with phase 0"). DP1–DP5 are now design decisions D31–D35 (design v1.1). Phase 0 starts. |
| v1.1 | 2026-11-08 | Approved by the owner. Phase 0 as built (see its "As built" note), and what its results change. **Phase 1:** a new step 1.3 for the glitch protection (D36), the last flash sector kept free (D37), the RP2350 stepping in the report, and a glitch check among the console tests. **Phase 2:** notes on lwIP (DHCP, send buffer) and the Tx power default. **Phase 5:** the 5 V recommendation (D38) in the README. Phase 1's steps are renumbered 1.1–1.12. |
| v1.2 | 2026-11-08 | Approved by the owner at the end of phase 1. Phase 1: the boot log (D39) and the page's test switches, added before step 1.10 so its 60+ console boots can be read in one go. The phase 1 code review's findings, their fixes, and the re-test before the merge (bench and 10 cold boots, the owner's choice). |
| v1.3 | 2026-11-09 | Approved by the owner at the end of phase 2. Phase 2 as built (see its "As built" note), with the owner's choices made during the phase (design D40, D41, and changes to D20 and D26), and what it means for phases 3 and 4. |
| v1.4 | 2026-11-10 | Approved by the owner at the end of phase 3. Phase 3 as built (see its "As built" note), with the owner's choices made during the phase (design D11, D12, D14), the phase 2 review's items for phase 3, and the test builds of step 3.7. |
| v1.5 | 2026-11-11 | Approved by the owner at the end of phase 4. Phase 4 as built (see its "As built" note), with the owner's choices made during the phase (design D42, D43), its code review, and the `.bss` budget for phase 5. |
| v1.6 | 2026-11-11 | Approved by the owner at the end of phase 5. Phase 5 as built (see its "As built" note): the security review of step 5.1, its fixes and their re-test in the console; the release workflow; the acceptance, its two fixes, and a rule for later releases from them; the code review and its two fixes; the owner's choices (the logo and the device's name, the fork test for the next release). |
| v1.7 | 2026-11-14 | Approved by the owner. For later releases: a tidy end to the injection (the CS counters and the bus priority), the owner's choice after the release. |
| v1.8 | 2026-11-14 | Approved by the owner. For later releases: a fourth wire for the console's data to U10 (the CubeSB board's GPX pad), an option the owner has. |
| v1.9 | 2026-11-14 | Approved by the owner. Step 5.3: the `v1.0.0` tag's run of the release workflow replaces the fork test. |
| v1.10 | 2026-11-14 | Approved by the owner. Release 1.0.1, a tidy end to the injection, from the research in [injection-end.md](research/injection-end.md): the owner chose DATA's input off as well, and a focused re-test. |
| v1.11 | 2026-11-14 | Approved by the owner. Release 1.0.1 as built (see its "As built" note): its code review, the owner's choices from it (the "DMA 64 KB" figure out of the re-test, no test for the new steps), and the re-test, which passed. |
| v1.12 | 2026-11-15 | Approved by the owner. Release 1.0.2, flash programmed from RAM only, from the research in [flash-from-ram.md](research/flash-from-ram.md). The owner chose a copy through a 256-byte RAM page, the zeros back in flash so that a payload upload or delete goes through it, and a re-test of every write path. |
| v1.13 | 2026-11-15 | Approved by the owner. Release 1.0.2 as built (see its "As built" note): its code review, the owner's choices from it (a second update in the re-test, from 1.0.2 to 1.0.2, and the Payloads tab's check after the delete; the release notes reworded; the range check kept), and the re-test, which passed. |
| v1.14 | 2026-11-15 | Approved by the owner. Release 1.0.3, the official gekkoboot r9.4, from the research in [gekkoboot-dol.md](research/gekkoboot-dol.md): the built-in gekkoboot was a CI build of r9.4 (version `undefined`), not the release's. The owner chose a DOL rebuilt from the release, a tool that rebuilds it and a local test that ties it to the release, and a focused re-test. |
| v1.15 | 2026-11-15 | Approved by the owner. Release 1.0.3 as built (see its "As built" note): its code review and its fixes (a test in CI that pins the asset, the research notes corrected, one sentence of the release notes reworded), and the re-test, which passed. |
| v1.16 | 2026-11-16 | Approved by the owner. Release 1.1.0, ordinary `.dol` files (D43, design v1.8), from the research in [dol-loader.md](research/dol-loader.md). The owner chose a stub that starts like gekkoboot's packer and fills low memory as gekkoboot does, refusing a `.dol` that reaches the payload image or low memory, and console tests with three `.dol` files and cold boots. |
| v1.17 | 2026-11-16 | Approved by the owner. Release 1.1.0 as built (see its "As built" note): its code review and fixes, the owner's choices from it (finding 2 watched in the console test; a `.dol` too big for its slot refused by the Pico, as before), gekkoboot's lowmem build as the third test `.dol`, and the console test, which passed. With design draft v1.9 (D43 as built). For later releases: larger ordinary `.dol` files. |
| v1.18 | 2026-11-20 | Approved by the owner. Release 1.1.1, no CS pull-up (D36, design v1.10), from the research in [cs-pull-up.md](research/cs-pull-up.md). The owner chose CS left as reset leaves it with the clock-qualified count, the CS test mode removed entirely, a README note on the ground wire and the CS counts, a handoff note for PicoBoot-DX, and a 20-boot console test on the second console. |
| v1.19 | 2026-11-20 | Approved by the owner. Release 1.1.1 as built (see its "As built" note): its code review and fixes, the owner's choices from it (bit 7 cleared on both Picos; release notes worded to the two test consoles), and the console test, which passed. |
| v1.20 | 2026-11-22 | Approved by the owner. A diagnostic build for CS timing (D44, design v1.13), from the research in [cs-timing-diagnostics.md](research/cs-timing-diagnostics.md): steps T1–T5, a test build and a console series on both consoles, then the owner's decision on what follows. The header names design v1.12, which it had missed. |
| v1.21 | 2026-11-22 | Approved by the owner. The diagnostic build's T1 as built (its own `timing.pio`, two words per period, a 32 KB record) and T2's code review with the owner's choices: the boot-log rows out of `.bss` in test builds, the record shows the undriven start itself, CI left as it is (test builds are compiled locally only). |
| v1.22 | 2026-11-22 | Approved by the owner. The diagnostic build as built (see its "As built" note): the console series and its results, and the owner's decision at T5: a wiring-quality number for the releases next, the code staying on its branch, both consoles back to 1.1.1. For later releases: the wiring-quality number. |

## How this plan works

- **Steps:** every phase is split into steps. Each step has a "done when" check.
- **Gates:** a phase starts only after the previous one is merged. Phase 0 can still change the design. If it does, the design gets a new version (v1.1, …) before phase 1 starts.
- **Branches:** one per phase: `phase-0-checks`, `phase-1-core`, `phase-2-wifi`, `phase-3-firmware-ota`, `phase-4-payload-ota`, `phase-5-release`. `main` holds only reviewed work.
- **Commits:** once this plan is approved, each step is committed on its phase branch without asking each time.
- **The end of a phase:**
  1. the owner's hardware tests;
  2. a code review on the branch (fixes go on the same branch);
  3. the design and this plan updated to match what was built;
  4. the owner's review;
  5. a merge into `main` with a merge commit, and the status lines saying "merged";
  6. the PDF exports regenerated (`docs/export-pdf.sh`).

## Who does what

| Who | Does |
|---|---|
| **Opus 5.5** (this session) | The plan, the core in C (loader, records, injection, measurement, Wi-Fi blob hook, flash writer, OTA and trial logic, payload slots, httpd integration), CMake and CI, local builds, and reviews of all subagent work |
| **Sonnet 5.5** subagents, each in its own git worktree | Independent parts with a written contract from Opus: the web UI, the JavaScript `.dol` converter, the Python tools (update-file builder, CLI client) |
| **Fresh Opus 5.5 reviewer** | The end-of-phase code review of the code this session wrote itself, so the review isn't anchored on the author's reading |
| **Fable 5.1** | The security review in phase 5. It wrote none of the code |
| **Owner** | Hardware tests on the DOL-001, pushes to GitHub (CI runs on push), reviews and approvals, merges |

## What's needed from the owner

| What | When |
|---|---|
| A Pico 2 W for the console, ideally a second one for bench tests | Phase 0 |
| The DOL-001 with the SD2SP2 and the HDMI adapter fitted, wired as today (3V3 feed) | Phase 0 |
| A current meter or shunt in the Pico's 3.3 V feed | Phase 0 |
| A phone (and ideally a second one with the other OS, for the captive-portal check) and a laptop | Phase 0, 2 |
| A GitHub repository `r3mko/PicoBoot-WiFi` with Actions enabled; pushing each phase branch | Phase 1 |
| Payload files to test with: Swiss r2119 and gekkoboot r9.4 (UF2 and `.dol`) | Phase 0, 4 |

No servers, DNS, accounts or tokens are needed.

## Design points for approval

The owner's answers on 2026-11-08 (equipment, console, readout) don't fit three design decisions. The owner approved these points with the plan, and they are now design decisions D31–D35 (design v1.1).

**DP1 → D31: the Pico measures its own boot timing (changes D10).**
- **How:** a third, passive PIO state machine watches CS. It doesn't touch the two injection programs.
  - It sets a PIO IRQ flag on the first CS falling edge, and the CPU, which is idle during the injection, timestamps that flag.
  - It counts every CS pulse until the injection ends.
  - The DMA transfer count gives the number of 1 KB transfers delivered.
- **The margin:** time of the first CS minus time of "PIO ready".
  - A positive margin together with a successful boot proves the PIO was in time.
  - A short CS count shows missed pulses.
- **Why:** no oscilloscope or logic analyzer is available.
- **Reuse:** the same counters become the boot diagnostics of D22.
- **What it can't do:** it can't see the supply ramp before the RP2350 leaves reset. That doesn't matter, because the margin is measured from the Pico's own start.

**DP2 → D32: a minimal Wi-Fi readout comes early (changes D28).**
- **Phase 0:** the measurement build is PicoBoot-DX v0.10 plus the DP1 counters. After the injection it starts an access point with a plain-text report page. It also serves two load-test URLs, a large download (Tx load) and an upload sink (Rx load).
- **Phase 1:** brings the Wi-Fi blob hook (D14) and the same minimal page forward from phase 2. The blob doesn't fit inside a 256K slot, and the results have to come out of a closed console.

**DP3 → D33: the rail check without a scope (changes D28 phase 0).**
- **Average current:** measured with the meter in four states:
  - AP off;
  - AP idle;
  - a client connected;
  - the download running.
- **Rail sag:** a functional stress test replaces the scope. The large download runs at full Tx power while Swiss loads a game from the SD2SP2, with the HDMI adapter on. What to watch for:
  - console glitches;
  - video artefacts;
  - a Pico brown-out (`HAD_BOR`, counted in the report);
  - Wi-Fi drops.

**DP4 → D34: the DOL-001 is the test console.**
- All hardware tests run on the owner's DOL-001 with the SD2SP2 and the HDMI adapter.
- The README says the DOL-101 is expected to work (same IPL path as PicoBoot) but is untested.

**DP5 → D35: no pre-releases; the first GitHub release is 1.0.0 (changes D24).**
- **Test builds:** come from CI artifacts on the pushed phase branch, or from Opus's local Docker build, which uses the same SDK and toolchain as CI.
- **Versions:** builds carry 0.N.0-dev (N = the phase) plus the git hash.

## Repository layout

```
CMakeLists.txt              loader + firmware targets, install UF2, update file
pico_sdk_2_import.cmake
src/common/                 layout.h (the flash map, D6), records, boot info, version
src/loader/                 the loader (D7)
src/firmware/               injection, measurement, Wi-Fi, web, OTA
src/firmware/pio/           PIO programs (injection from PicoBoot-DX, CS counter)
linker/                     linker overrides (loader 16K, firmware 256K slot)
web/                        static UI sources (gzipped into the firmware)
tools/                      Python: process_ipl, uf2 packer, update builder, CLI
tests/                      Python tests, PIO simulator, host C tests, layout check
hwtest/dx-measure/          phase 0 measurement build (PicoBoot-DX + DP1 + DP2)
docker/Dockerfile           local build image: SDK 2.3.1, picotool 2.3.1, arm-none-eabi-gcc
docs/                       design, plan, research, API, update-file format
```

`src/common/layout.h` is the only place the flash map is written down. The linker scripts, the tools and the layout check read it.

## Phase 0: hardware checks

**Branch:** `phase-0-checks`, **merged** into `main` on 2026-11-08. **Goal:** measure what the design couldn't know, before any core code exists.

| # | Step | Who | Done when |
|---|---|---|---|
| 0.1 | **Measurement build** `hwtest/dx-measure`. Start from PicoBoot-DX v0.10 (pico2_w) and add: the DP1 counter state machine; after the injection, the pico-examples access point (WPA2, test password) with `/report` (text), `/stream` (large download) and `/sink` (upload). The report shows the margin, the CS count, transfers delivered, injection complete or stalled, reset reason (`HAD_POR`/`HAD_BOR`), time to Wi-Fi up and uptime. | Opus | Builds in Docker; the PIO simulator shows the injection unchanged with the counter running |
| 0.2 | **Bench check**: Pico 2 W on USB, no console | Owner | The AP is visible; `/report` says "no CS seen"; `/stream` downloads |
| 0.3 | **Cold boots in the console**, 3V3 wiring as today: 10× gekkoboot r9.4, 10× Swiss r2119, reading `/report` after each | Owner | 20 results recorded; the radio works on the console's 3.3 V; every boot good |
| 0.4 | **Warm board**: 5 boots after 30 minutes of play | Owner | Recorded, compared with 0.3 |
| 0.5 | **Current**: AP off (during the injection), AP idle, client connected, `/stream` running (DP3) | Owner | Four values recorded |
| 0.6 | **Range**: phone RSSI at 1, 3 and 5 m with the console closed, in two board positions | Owner | Recorded |
| 0.7 | **Stress test** (DP3): `/stream` while Swiss loads a game from the SD2SP2, HDMI adapter on | Owner | Glitches, video, brown-outs and Wi-Fi drops recorded |
| 0.8 | **Results** in `docs/research/phase0-results.md`, design changes (if any) as a new design version | Opus | The owner approves the results and any design change |

**As built (2026-11-08):**
- **Moved forward from phase 1** (needed for the measurement build): `docker/Dockerfile`, `tools/process_ipl.py`, gekkoboot r9.4 in `assets/ipl/`, and `tests/pio_sim.py`.
- **Added to the measurement build** while testing:
  - D8's warm-reboot rule, with *Reboot* buttons to test it;
  - test switches: built-in IPL, Wi-Fi 60 s late, CS pull-up;
  - a check of the last flash page for the E10 block;
  - a second, clock-qualified CS counter;
  - the pin levels at "PIO ready".
- **Step 0.3b added:** the glitch series after boot 12 of step 0.3 (parts 1–3).
- **Step 0.5:** measured the 3V3 voltage instead of the current, because the owner's setup can't put a meter in series.
- **Bug found on the bench and fixed:** lwIP drops DHCP requests when `LWIP_DHCP` is off, even for a DHCP *server*.

**Code review of phase 0** (a fresh Opus 5.5 reviewer, 2026-11-08):
- **Overall:** no serious bugs. The injection programs are byte-identical to PicoBoot-DX v0.10, the build is clean under `-Wall -Wextra`, the tests pass, and the warm-reboot and flash-write constraints hold.
- **The measurement build stays as it was measured.** Its findings are requirements for the phase 1 code that reuses it:

| # | Finding | Where it's handled |
|---|---|---|
| 1 | The clock-qualified count must check that CS is still low when CLK rises (`jmp pin`); otherwise a glitch plus a CLK pulse while CS is high still counts. Already corrected in the PicoBoot-DX handoff | 1.3, 1.4: in the injection and the counters, with simulator cases for CLK while CS is high (`pio_sim` gets `JMP !X` and `JMP PIN`) |
| 2 | Recovery from a torn record write erased the newer records in the same sector | 1.5: erase only at a sector start; host test |
| 3 | The phase 0 power-on rule also injects on BOR and RUN; D8 is HAD_POR only. Keep POWMAN `scratch[0]` for the marker | 1.6 |
| 4 | A failed `tcp_write` with nothing in flight is never retried | 2.2: retry from the poll callback (or lwIP httpd handles it) |
| 5 | A client FIN cuts the response short | 2.2: finish the response, then close |
| 6 | The `tcp_abort` fallback after `tcp_close` would be unsafe inside a callback | 2.2: no such fallback, or return `ERR_ABRT` |
| 7 | Raw and clocked counts are read at different moments, so a false GLITCH is possible | 1.4: snapshot while CS is high, or re-read until stable |
| 8 | `dma_channel_abort()` without first clearing the channel's EN bit (RP2350-E5) | 1.7 |
| 9 | The counters use PIO IRQ flags 0 and 1, which are only safe outside the injection's PIO block | 1.4: use flags the injection doesn't use, or keep them in another block |
| — | Test gaps: the counter's X set-up and read path, and coupling between the counters and the injection, aren't exercised | 1.4 |

## Phase 1: core

**Added during the phase (2026-11-08), for step 1.10:**
- **The boot log (D39):** the last 32 boot reports on the page.
- **Page buttons:**
  - the payload for the next power-ons (payload 1, payload 2, built-in);
  - the CS test mode (PicoBoot's pull-down, for the glitch check), stored as a test flag in boot control.

**Branch:** `phase-1-core`, **merged** into `main` on 2026-11-08. **Done when (D28):**
- the console boots a payload through the loader on a cold and on a warm board;
- a warm reboot doesn't inject;
- the boot-time budget is met (D10);
- the clock is set (D27).

| # | Step | Who | Done when |
|---|---|---|---|
| 1.1 | **Skeleton**: CMake with the loader and firmware targets, `layout.h` (last flash sector kept free, D37), linker overrides (loader ≤16K, firmware ≤256K), CI (ubuntu-26.04: build, tests, layout check) with the phase 0 `docker/Dockerfile` setup, LICENSE (GPL-2.0-only, D29), README stub | Opus | CI builds both ELFs; the layout check passes |
| 1.2 | **Injection port** from PicoBoot-DX: PIO programs, `pio.c`, payload check (base address as a parameter), `ipl.h` from `tools/process_ipl.py` (both from phase 0), and the remaining PicoBoot-DX tests next to `tests/pio_sim.py` | Opus | CI tests green, PIO simulator included |
| 1.3 | **Glitch protection** (D36):<br>• CS pull-up before the PIO starts;<br>• a clock-qualified count of the 224 skipped pulses in `on_transfer`, CLK rising while CS is still low (`jmp pin`), as in the PicoBoot-DX handoff (`docs/handoff/`).<br>Simulator tests: with a CS glitch without clocks before the IPL read, PicoBoot-DX's programs shift the payload (phase 0's test) and the new ones don't; without a glitch, the output is bit-identical; every address clock of the first payload transfer is seen | Opus | Simulator tests green, including the phase 0 glitch case |
| 1.4 | **CS counters and boot report** (D31): the raw and clocked counters from phase 0 as firmware code, plus the RP2350 stepping (for RP2350-E9) | Opus | The simulator test counts the right pulses and transfers |
| 1.5 | **Records** for boot control and settings (D23): two sectors, sequence number, CRC32, newest valid record wins | Opus | Host C tests pass, including a torn write that falls back to the previous record |
| 1.6 | **Loader** (D7, D8):<br>• power-on rule (`HAD_POR` + POWMAN scratch marker);<br>• slot choice as one pure function;<br>• boot info in POWMAN scratch;<br>• `rom_chain_image()`;<br>• the other slot if one is empty, BOOTSEL if both are;<br>• no clock setup (`pico_minimize_runtime`) | Opus | Host tests cover the decision table; the size check is ≤16K |
| 1.7 | **Firmware start-up**: read boot info; power-on → inject payload 1, 2 or built-in (payload read through the untranslated XIP window); warm → no injection | Opus | On the bench, the loader chains into slot A and `/report` shows a warm boot |
| 1.8 | **Wi-Fi blob hook** (D14, DP2): the blob is loaded from Wi-Fi A/B at its fixed address. The minimal AP and `/report` page from 0.1 run inside the slot, with a *reboot* button for the warm-reboot test | Opus | The AP comes up from a slot build with the blob outside the image |
| 1.9 | **Install UF2**: `tools/mkuf2.py` packs absolute regions (rp2350-arm-s): loader, blank boot control and settings, Wi-Fi A (Wi-Fi-only blob, built from the SDK header) and slot A, without picotool's E10 block (D37). CI artifact `picoboot-wifi-install.uf2` | Opus | Python tests for the packer; the UF2 parser shows the right ranges and nothing at or above `0x3FF000`; a bench install over PicoBoot-DX keeps payload 1 |
| 1.10 | **Console tests**:<br>• 10 cold boots each with gekkoboot and Swiss;<br>• warm board;<br>• *reboot* from the page while a game runs (the console keeps running and the report says "warm, no injection");<br>• the glitch check: 40 cold boots with a test switch that restores PicoBoot's CS pull-down, where every boot must start and any glitch must show as survived in the report.<br>Compare the margin with phase 0 | Owner | Results recorded; the margin difference gives the loader's cost; no blue screens |
| 1.11 | **Budget and clock**: set the D10 budget and the D27 clock and flash divider from phase 0 and 1.10. If the loader is too slow, the R1 fallback (loader starts the PIO) | Opus + owner | Values written into the design; budget met |
| 1.12 | **End of phase**: fresh Opus review, fixes, design and plan updated, merge | Opus, owner | Merged |

**Code review of phase 1** (a fresh Opus 5.5 reviewer, 2026-11-08):
- **Overall:** no high or medium defects. The loader and D8 rule, the injection port, records and ring log, flash writes and payload bounds held when traced. The build is clean, the tests pass, and the built install UF2 places every part at its `layout.h` offset with nothing at or above `0x3FF000`.
- **Re-test before the merge** (the owner's choice): on the bench, a test UF2 blanks slot A and the board must come up in BOOTSEL (finding 3); after a reinstall, the page; then 10 Swiss cold boots.

| # | Finding (all low) | Where it's handled |
|---|---|---|
| 1 | The Wi-Fi blob header check could wrap (`clm_offset + clm_len`), so a bad header could send the CYW43 driver past the area | Fixed in phase 1; host test. Checking the blob's SHA-256 after the injection: phase 3 (D14) |
| 2 | `ringlog_read()` rechecked every entry's CRC for each entry returned, about 1,000 CRCs over uncached flash per page load | Fixed: one pass |
| 3 | With both slots empty, the loader's reboot into BOOTSEL waits on the watchdog tick, which only the SDK's clock setup (skipped) starts | Fixed: the loader starts the tick. Bench test before the merge |
| 4 | The DNS server could reply with bytes past the query; the DHCP option search could return an option past its buffer (both from pico-examples) | Fixed. The phase 5 security review looks at both again |
| 5 | The D36 check is "CLK reads high", not "CLK rises": a false CS pulse while CLK already reads high still counts | **Not changed** (the owner's choice): a known limit in D36, not seen on hardware |
| 6 | The CS pull-down test flag lives in boot control: one POST turns the D36 pull-up off for every later power-on | 2.4: the release web UI has no test switches; the firmware ignores the flag outside test builds; bit 7 stays reserved |
| 7 | 250 MHz was a constant, and after the injection the clock dropped to half of it (125 MHz) | Fixed: `PICOBOOT_WIFI_INJECTION_KHZ` and `PICOBOOT_WIFI_FLASH_CLKDIV` are build settings (D27); after the injection the clock returns to the SDK's 150 MHz, as on warm boots |
| 8 | The ring log holds 17 to 32 entries, not "the last 32" | D39 says "up to 32" |
| 9 | A button's failed boot control write still looked successful | Fixed: the page says so |
| 10 | CI uploaded the stand-alone `firmware.uf2`, which writes over the loader, boot control and Wi-Fi A | Fixed: only the install UF2 and the ELFs |
| — | Test gaps: `payload_check()`, the Wi-Fi blob check, the built install UF2 against the built parts | Added. Not added: a power cut during a ring-log erase, sequence wrap-around in the log, a shared boot-info encoder (later phases) |
| — | From the boot 95 page: the log's margin column showed the false CS edge on glitch boots | Fixed: it shows the margin to the first real transfer |

## Phase 2: Wi-Fi

**Branch:** `phase-2-wifi`, **merged** into `main` on 2026-11-09. **Done when (D28):** a phone reaches the status page from a closed console.

| # | Step | Who | Done when |
|---|---|---|---|
| 2.1 | **Settings** on the records from 1.4: SSID, password, channel, country, Tx power, auto-off, LED; defaults from D30 | Opus | Host tests; settings survive a reboot |
| 2.2 | **Network**: AP with WPA2-AES (D30), DHCP, DNS answering every name with `192.168.4.1`, redirects for the phone probe URLs, lwIP httpd with custom files.<br>From phase 0: keep `LWIP_DHCP` on (without it lwIP drops DHCP requests), and size the TCP send buffer for throughput (8 × MSS gave 0.4–0.95 MB/s) | Opus | iOS and Android open the page by themselves on the bench |
| 2.3 | **API contract** `docs/api.md` (status, settings, diagnostics) and the JSON handlers | Opus | Handlers answer as specified |
| 2.4 | **Web UI**: plain HTML/CSS/JS, status, diagnostics and settings pages, default-password banner (D19), gzip build step | Sonnet (worktree) | Works against the API on the bench; under about 30 KB gzipped; Opus reviewed |
| 2.5 | **Diagnostics** (D22): the boot report from 1.3, plus the loader decision, the payload choice and why, time to Wi-Fi up | Opus | Shown on the status page |
| 2.6 | **Auto-off** (D20), **Tx power** (the `qtxpower` iovar), **LED** patterns (D26). The default Tx power is set lower than the maximum, since phase 0 found plenty of range (−42 dBm at 5 m) | Opus | Auto-off after 10 minutes without a client; Tx power changes the RSSI |
| 2.7 | **Console tests**: captive portal on both phones, settings, banner, auto-off, Tx power against range and current; injection results the same as in phase 1 | Owner | Recorded |
| 2.8 | **End of phase**: fresh Opus review, fixes, docs, merge | Opus, owner | Merged |

**As built (2026-11-09):** results in [phase2-results.md](research/phase2-results.md); test builds are `0.2.0-dev`.

| Step | What was built | The owner's choices |
|---|---|---|
| 2.1 | Settings as a record (D40), with value checks; a stored value that isn't allowed falls back on its own default | Default channel 6; Tx power in named levels |
| 2.2 | lwIP httpd in place of the raw TCP server. Every answer is a custom file with its own headers: 302 to the start page for unknown paths (the captive portal), 204 for `/favicon.ico`. httpd copies answers into its segments, so one freed at close can't be read again by a retransmission. The firmware got 25 KB smaller (the old server's `sscanf`) | |
| 2.3 | `docs/api.md`; a small JSON writer and flat-object reader; the handlers; POST bodies only as `application/json` (a CSRF guard). An access-point restart restarts the DHCP server too, because the network interface comes back with another number | D41: Wi-Fi changes restart only the access point, at once, and stay on trial until a client comes back through it; the password is write-only |
| 2.4 | The web UI as one page with tabs (6.4 KB gzipped) and `tools/mkfsdata.py`, built by a Sonnet subagent in a worktree and reviewed. The phase 1 test page moved to `/test`, in test builds only; release builds also ignore the CS test flag (phase 1 finding 6) | One page with tabs; light or dark as the phone; the test page kept at `/test` |
| 2.5 | The boot report records boot control's payload choice, so the diagnostics say when the built-in payload went instead. The boot log format changed (version 2) | |
| 2.6 | Auto-off powers the CYW43 down (`cyw43_arch_deinit()`), then the firmware sleeps between interrupts. Tx power through the `qtxpower` iovar, from the main loop. The LED patterns of D26 | Tx power 5, 10 and 15 dBm and the chip's limit; after a stall the LED blinks for as long as Wi-Fi is on. Auto-off runs up to 1.5 minutes late (the chip's client timeout): accepted |
| 2.7 | Console tests all passed | 5 Swiss boots instead of 10 accepted |

**For later phases:**
- **Phase 3:**
  - uploads need a streaming POST path: net.c now collects bodies of up to 1 KB;
  - `LWIP_HTTPD_POST_MANUAL_WND` holds the upload back while a sector is erased (D18);
  - httpd also calls `httpd_post_finished()` when a connection closes mid-body, so the writer must tell a complete upload from a cut one;
  - D41's trial (in `settings.c`) is a small model of D12's trial and keep.
- **Phase 3 and 4:** uploads use `application/octet-stream` (D18), which, like JSON, needs a CORS preflight from another site, so the CSRF guard carries over. New UI parts go into `src/web/index.html` as tabs.
- **Phase 4:** the payload choice moves from the test page into the UI.

**Code review of phase 2** (a fresh Opus 5.5 reviewer, 2026-11-09):
- **Overall:** no high defects. The httpd answer lifetimes, the POST paths (split bodies, refusals, connections closing mid-body), the access-point restart and the D41 sequences held when traced against the lwIP and CYW43 sources. The JSON reader took about 5 million fuzzed inputs under ASan and UBSan without a memory error or an accepted body that the settings checks would refuse. A release-version build has no test page.
- **Re-test before the merge** (the owner's choice): a short bench test. Both phones open the UI; a POST with a wrong Host gets 403; a country change restarts the access point and phones rejoin; the test page's buttons still work.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Medium:** DNS rebinding got past the CSRF guard. The Pico's DNS answers every name with its own address, so a page opened elsewhere could point its own name at the Pico and send same-origin POSTs without a preflight. It could change settings, reboot, or set a known password | Fixed: POSTs must say `Host: 192.168.4.1` (403 otherwise), for the API and the test page; `docs/api.md` says so. Phase 4's backups, which include the settings, are to be POST-only for the same reason |
| 2 | If the chip didn't take the access point down, the restart kept the old settings (the driver skips the set-up while the BSS is up), and an old client could confirm a change that never ran | Fixed: the restart checks the BSS and retries; if it stays up, the change can't be confirmed and is undone at the next start |
| 3 | Undoing an unconfirmed change brought back every setting saved with it, not only the Wi-Fi ones | Fixed (the owner's choice): only the SSID, password, channel and country go back, as D41 says; host test |
| 4 | Test builds: the test page printed the SSID unescaped, and its POSTs had no guard | Fixed: escaped, and the Host check covers them |
| 5 | A `.DS_Store` or editor backup in `src/web/` stopped the build | Fixed: skipped; test |
| 6 | The UI said "nothing was saved" when a save's answer was lost, though a Wi-Fi change may have gone through | Fixed: it says the outcome is unknown, waits for the Pico and shows its settings |
| 7 | Out of memory, httpd had no error answer of its own: the client got a 302 to the start page, which the UI could take for success | Fixed: a fixed 503 answer |
| 8 | The diagnostics answer (about 18 KB at most today) had little room in its 20 KB buffer for later fields | Fixed: 32 KB |
| — | **For phase 3:**<br>• once a watchdog runs (3.4), the idle loop after auto-off must stop it or feed it, or auto-off becomes a reboot loop;<br>• records filter by format version, so an older firmware in the other slot could erase a newer record; the record writer must never erase the newest record of any version before slots can switch;<br>• `wifi.c`'s restart, confirm and auto-off logic has no host tests; D12's trial should be pure C, as D41's is | Phase 3 steps 3.3 and 3.4 |

## Phase 3: firmware OTA

**Branch:** `phase-3-firmware-ota`, **merged** into `main` on 2026-11-10. **Done when (D28):** an update, a failed trial and a switch all work in the console without USB.

| # | Step | Who | Done when |
|---|---|---|---|
| 3.1 | **Update-file format** `docs/update-format.md` (D11): header, slot image, optional blob, SHA-256s | Opus | Format reviewed by the owner |
| 3.2 | **Update-file builder** `tools/mkupdate.py`, with tests and a CI artifact | Sonnet (worktree) | Tests green; the output matches the format; Opus reviewed |
| 3.3 | **Flash writer**: streaming POST with a manual TCP window, sector erase and program through `flash_safe_execute()`, SHA-256 on the fly (`pico_sha256`) and after reading back, Wi-Fi blob pairing (D14), boot control "pending" | Opus | Bench: an update lands in slot B and verifies; a corrupted file is refused |
| 3.4 | **Trial, keep and fallback** (D12, D13):<br>• kept after a good power-on injection plus Wi-Fi, or on *Keep*;<br>• "trial failed";<br>• crash-loop counter;<br>• watchdog and HardFault → reboot;<br>• loader decision tests extended | Opus | Host tests; on the bench a crash build falls back |
| 3.5 | **Firmware page**: upload with progress, both slots, *Keep*, *Boot slot X* | Sonnet (worktree) | Works on the bench; Opus reviewed |
| 3.6 | **CLI** `tools/picoboot-wifi.py status / update` (standard library only) | Sonnet (worktree) | Updates a bench board; Opus reviewed |
| 3.7 | **Test builds**: a firmware that crashes at start-up and one that crashes after 5 s, as CI artifacts | Opus | Built |
| 3.8 | **Console tests**:<br>• update A→B, kept after a power-on;<br>• crash build → fallback;<br>• switching slots;<br>• power cut during an upload → the old firmware boots;<br>• update with a different blob (the Bluetooth variant as a stand-in) | Owner | Recorded |
| 3.9 | **End of phase**: fresh Opus review, fixes, docs, merge | Opus, owner | Merged |

**As built (2026-11-09):** results in [phase3-results.md](research/phase3-results.md); test builds are `0.3.0-dev`.

| Step | What was built | The owner's choices |
|---|---|---|
| 3.1 | `docs/update-format.md`: a 256-byte header with the layout and loader versions, then the image and the blob | The blob always in release files; downgrades allowed, with a warning |
| 3.2 | `tools/mkupdate.py` (Sonnet, reviewed), `build/picoboot-wifi-update.bin` on every build, a CI artifact | |
| 3.3 | `update.c` (pure, host tested: whole uploads into a simulated flash) and `updater.c`; uploads stream from httpd's POST callbacks straight to flash, so no manual TCP window was needed. A host test feeds the files `mkupdate.py` makes through `update.c`. An upload takes about 4 s | |
| 3.4 | Loader version 2: the session in POWMAN scratch, a crash being any reset without the firmware's clean-reboot word. The firmware's side in `slots.c` (pure). A watchdog (8 s) and a fault handler. No updates while on trial. Records leave another version's record alone (phase 2 review) | 3 crashes for the crash loop; the fallback for good |
| 3.5, 3.6 | `POST /api/boot` (keep, boot a slot) and each slot's version from its binary info; the Firmware tab (the UI is 9.5 KB gzipped) and `tools/picoboot-wifi.py`, each by a Sonnet subagent in a worktree, reviewed | |
| 3.7 | `PICOBOOT_WIFI_TEST_CRASH=startup` / `after5s` and `PICOBOOT_WIFI_BLOB=marked`, as CI artifacts | |
| 3.8 | Console tests all passed after one fix: a failed trial's note in the session word also blocked a new trial of that slot in the same session; the firmware now clears it when it sets up a new trial. The SDK's Bluetooth blob, the planned stand-in for a new blob, didn't come up without Bluetooth support (its trial failed and was undone); the stand-in became the Wi-Fi blob with a marker | |

**Code review of phase 3** (a fresh Opus 5.5 reviewer, 2026-11-10):
- **Overall:** the normal paths the bench and console tests covered held up. The findings were in paths those tests didn't reach: a firmware running in place of the active one, a switch to a slot that only counted as installed, the keep rule, and a cut-off upload. Findings 1–4 were confirmed by host simulations.
- **Re-test before the merge:** bench (the access point reported up, keep, a cut-off upload followed by another, the Firmware tab after a USB install) and one console power-on for the keep.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **High:** an update could go into the active slot, after a fallback boot or in the half second before a switch's reboot. It then started without a trial or a way back. And a cut-off upload over an old image could leave a slot the boot ROM accepts (the image has no hash) | Fixed: updates only from the active firmware (`slots_update_target()`); a firmware started in place of the active slot takes over; no upload while a reboot is due, and no other POST while an upload runs; the loader never starts an empty slot; the image's first sector is written last. Host tests |
| 2 | **Medium-high:** a trial was kept even if Wi-Fi never came up: the driver doesn't report a failed start | Fixed (the owner's choice): kept only if the chip reports the AP up; on trial without Wi-Fi, a reboot as a crash |
| 3 | **Medium:** "Boot slot X" to a slot that only counted as installed (after a USB install it may hold anything) made it active at once | Fixed: it goes on trial; after a USB install slot B counts as empty; only a slot with a PicoBoot-WiFi firmware is offered |
| 4 | **Medium:** a cut-off upload left the SHA-256 hardware claimed; the next upload waited for it until the watchdog reset, counted as a crash | Fixed: an unfinished hash is released first |
| 5 | **Low:** the UI and CLI reported "on trial" after an upload without checking that the new firmware came up | Fixed: both compare the running slot with the one installed; CLI test |
| 6 | **Low, for phase 4:** once a record's version changes, a firmware of the other version rewrites its own record in place, without the power-cut safety | Phase 4: allow for it when a record's version changes |

**For phase 4:**
- **Boot control** keeps its format: a new `BOOTCTL_VERSION` needs a loader that reads it, so a loader version 3 and a USB install.
- **The payload and "original IPL once" choices** go into `POST /api/boot` beside `keep` and `slot`.
- **Backups** that include the settings (the Wi-Fi password) are to be POST-only (phase 2 review, DNS rebinding).
- **The update writer's pattern** (pure C behind an io struct, host-tested with simulated flash and interop with the Python tools) suits the payload upload of step 4.1.

## Phase 4: payload OTA

**Branch:** `phase-4-payload-ota`. **Done when (D28):** Swiss and gekkoboot uploaded and chosen over Wi-Fi.

| # | Step | Who | Done when |
|---|---|---|---|
| 4.1 | **Payload upload** (D15, D16): streaming UF2 parser (any family, blocks from `0x10080000`), raw payload, checks, write to the slot that isn't booting, metadata (name, size, SHA-256, fingerprint) in boot control | Opus | Host tests for the parser with the real Swiss and gekkoboot UF2s; bench upload |
| 4.2 | **USB rule** (D17): fingerprint check before the injection, record after it | Opus | A USB install over a payload-2 choice boots payload 1 |
| 4.3 | **"Original IPL once"**: a one-shot flag in boot control, cleared at the next power-on | Opus | Bench and host tests |
| 4.4 | **Backups**: streaming downloads of payload 1/2, the firmware slot and settings | Opus | Downloaded SHA-256 matches the status page |
| 4.5 | **`.dol` converter** in JavaScript (port of `process_ipl.py` and the UF2 payload format), with a Node test in CI against the Python output | Sonnet (worktree) | Byte-identical to Python for gekkoboot r9.4 and Swiss r2119; Opus reviewed |
| 4.6 | **Payload page**: upload `.dol`/`.uf2`/`.bin`, choose, delete, download | Sonnet (worktree) | Works on the bench; Opus reviewed |
| 4.7 | **CLI** payload commands: upload, choose, convert | Sonnet (worktree) | Works on the bench; Opus reviewed |
| 4.8 | **Console tests**:<br>• Swiss and gekkoboot as UF2 and as `.dol`;<br>• choosing each;<br>• the USB rule;<br>• "original IPL once";<br>• power cut during a payload upload;<br>• backup | Owner | Recorded |
| 4.9 | **End of phase**: fresh Opus review, fixes, docs, merge | Opus, owner | Merged |

**As built (2026-11-11):** results in [phase4-results.md](research/phase4-results.md); test builds are `0.4.0-dev`.

| Step | What was built | The owner's choices |
|---|---|---|
| 4.1 | Boot control format 2 (the names) and loader version 3, one USB install; records of any format, written to alternate sectors (phase 3 review, finding 6). `payloadup.c` (pure, host tested with the real Swiss and gekkoboot UF2s) and `payloads.c`: `POST /api/payload`, choose, rename, delete, the status. `sha.c` shares the SHA-256 hardware | D42: names you choose; uploads into payload 2 while the built-in boots; the chosen payload can't be deleted |
| 4.2 | The USB rule: a fingerprint of payload 1, recorded after a complete injection, so the console's power-on shows the switch; a choice, rename or upload over Wi-Fi settles it. Pure functions in `payload.c`, host tested | |
| 4.3 | Original IPL once: no injection, then Wi-Fi 1.5 s later | |
| 4.4 | `GET /api/backup/…`: a payload as it sits in flash, the running firmware as an update file (its header made on the Pico, byte-identical to `mkupdate.py`'s), the settings without the password; streamed through httpd's dynamic read, one transfer at a time | D42: plain downloads; payloads raw; only the running firmware |
| 4.5 | `src/web/dol.js` (Sonnet, reviewed): byte-identical to `process_ipl.py`, by a Node test that includes a round trip through the release payloads | |
| 4.6 | The Payloads tab, and the backup and restore links (Sonnet, reviewed); the UI is 12.6 KB gzipped | |
| 4.7 | The CLI's payload, IPL-once, backup and restore commands (Sonnet, reviewed), with its own copy of the converter, pinned to `process_ipl.py` by a test | |
| 4.8 | Console tests all passed, with two fixes. The USB rule had recorded the install at the drag-and-drop's reboot on USB power, so the console's power-on didn't show it. The margin had fallen to 156.5 ms: `.bss`, zeroed on the boot ROM's clock before the injection, had grown to 77 KB. With the upload writers and lwIP's pools in `.uninitialized_data` (20 KB) it's 163.9 ms, 1.6 ms below PicoBoot-DX, and the layout check keeps `.bss` under 24 KB | D43: a loader stub for ordinary `.dol` files, later |

**Code review of phase 4** (a fresh Opus 5.5 reviewer, 2026-11-11):
- **Overall:** no high findings; one medium and ten low, mostly power-cut timing and wrong states that the console tests didn't reach. All fixed in `721d396`.
- **Re-test before the merge:** bench and console, in [phase4-results.md](research/phase4-results.md).

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Medium:** an update to a phase 3 firmware was accepted. It reads only boot control format 1, so it ran on the defaults: payload 1 whatever the choice, and its trial never kept | Fixed: an update file whose loader requirement is below 3 is refused (`needs_usb_install`). Host test |
| 2 | **Low:** a power cut in the erase of a delete, or of an upload into payload 1, could leave the old header, which the USB rule took for a new payload | Fixed: the first page is zeroed before boot control records the change |
| 3 | **Low:** the fingerprint was in effect the size: builds of a payload share their first 256 bytes | Fixed: also 64 words spread over the payload and its last 256 bytes. Host test |
| 4 | **Low:** the CLI took a lost answer for a success when the target held the same payload already | Fixed: the target must be the choice too. CLI test |
| 5 | **Low:** an over-long `X-Name` was dropped instead of refused | Fixed: 400 `bad_value` |
| 6 | **Low:** the test page didn't escape the payload names | Fixed |
| 7 | **Low:** wrong states: a USB-install notice after a firmware USB install, "now the choice" after another choice, the notice on an IPL-once boot, a payload named for a skipped boot, downloads during an upload | Fixed |
| 8 | **Low:** `docs/api.md` out of step in places | Fixed |
| 9 | **Low:** on an IPL-once boot, Wi-Fi started during the console's own IPL read (D9) | Fixed: Wi-Fi waits 1.5 s |
| 10 | **Low:** lwIP's pools in uninitialised memory rely on lwIP setting what it reads | `MEMP_MEM_INIT`: lwIP zeroes them in `lwip_init()`, after the injection |
| 11 | **Low:** the USB rule had no host tests | Fixed: two pure functions, host tests of its sequences |

**For phase 5:**
- **`.bss`** costs about 0.13 ms of the margin per KB; the layout check fails it over 24 KB in the firmware.

## Phase 5: release 1.0

**Branch:** `phase-5-release`. **Done when (D28):** the owner's hardware test passes on a fresh install and on an upgrade from PicoBoot-DX.

| # | Step | Who | Done when |
|---|---|---|---|
| 5.1 | **Security review** of the whole network side: httpd handlers, upload parsers, flash-writer bounds, settings input, DNS and DHCP | Fable 5.1 | Findings reported; fixes on the branch, reviewed by Opus |
| 5.2 | **README** (install, upgrade from PicoBoot-DX, wiring with the 5 V recommendation of D38, default password, recovery, DOL-101 note), `docs/api.md` final | Opus | Owner reviewed |
| 5.3 | **Release workflow**: on a tag, build and attach the install UF2, the update file and checksums | Opus | A test tag on a fork produces the assets |
| 5.4 | **Acceptance** on the DOL-001: fresh install, upgrade from PicoBoot-DX, OTA from a 0.4.0-dev build to 1.0.0 | Owner | Passed |
| 5.5 | **Merge and tag 1.0.0** | Owner | Released |

**As built (2026-11-11):** results in [phase5-results.md](research/phase5-results.md); test builds were `0.5.0-dev`, the release is `1.0.0`.

| Step | What was built | The owner's choices |
|---|---|---|
| 5.1 | The security review by Fable 5.2 (the plan named 5.1, the model then current), below; its fixes in `360d8dd`, re-tested in the console | L1 accepted and documented |
| 5.2 | The README for release 1.0: installing, upgrading from PicoBoot-DX, the wiring with D38's 5 V, the default password, security, recovery, the DOL-101 note. `docs/api.md` final, the release notes in `docs/releases/1.0.0.md` | Reviewed: PicoBoot-DX's logo added to the README; the device named PicoBoot-WiFi on the page, in its messages and in the README (`c468ee9`), not "PicoBoot", which could be taken for upstream; the Wi-Fi name stays `PicoBoot-XXXX` (D30); the selects' arrow inset from the edge (`527823a`); the release notes approved |
| 5.3 | `.github/workflows/release.yml` (D24): on a tag `vX.Y.Z` that matches `CMakeLists.txt` and has release notes, it builds, tests, checks the built version and makes a draft release with the install UF2, the update file, the CLI and `SHA256SUMS`. A release's version has no git hash. A host test took only test-build versions, so a real tag would have failed: fixed in `25da3ce`. The release build is byte-identical from a clean checkout | The fork test was left for the next release: the `v1.0.0` tag would be the workflow's first run, which only makes a draft. Instead, its steps up to the draft were run on a clean checkout, and gave the files of the acceptance. The `v1.0.0` tag's run (43059931867, 2026-11-11) then made the draft with all four files, uploaded by `github-actions[bot]`, and its `SHA256SUMS` match the acceptance's files. That run replaces the fork test |
| 5.4 | Passed on the third build of 1.0.0, with a margin of 165.2 ms, 0.3 ms below PicoBoot-DX. The first two runs found two bugs, both from review fixes:<br>• the DHCP server ignored renewals, so with 5.1's 10-minute leases every client lost its address at 10 minutes (`9a3b17b`, with a host test);<br>• `invalidate()`, from the phase 4 review, programmed flash from a `const` array in flash, so every payload upload and delete hung until the watchdog reset the Pico (`df962b8`) | |
| 5.5 | Merged into `main` on 2026-11-11. The owner tags `v1.0.0` and publishes the workflow's draft once its checksums match the release build's ([phase5-results.md](research/phase5-results.md)) | |

**Security review** (step 5.1, Fable 5.2, 2026-11-11):
- **Overall:** no critical, high or medium findings; seven low and five informational. The upload parsers, the flash writers, the header and JSON parsers, the records and the DNS and DHCP servers were fuzzed on the host (about 20 million inputs, with AddressSanitizer and UndefinedBehaviorSanitizer). The fixes are in `360d8dd`.
- **Re-test:** in the console, build `0.5.0-dev+3806ed3`: passed, in [phase5-results.md](research/phase5-results.md).

| # | Finding | Where it's handled |
|---|---|---|
| L1 | A page a client opened elsewhere can read the GET answers through DNS rebinding (httpd gives the GET path no headers for a Host check) | Accepted by the owner: no GET answer holds a secret; documented in `docs/api.md`, D19 and the README |
| L2 | A failed `malloc` panicked (the SDK's default), a crash D12 counts; the 503 paths never ran | `PICO_MALLOC_PANIC=0` |
| L3 | The DHCP option search could read past the message | A minimum length per option, and the search stays within the bytes received |
| L4 | Eight made-up MAC addresses held all DHCP leases for a day | 10-minute leases; an expired one is taken over by a request too |
| L5 | Restoring a crafted settings file could set a password the owner doesn't know | The page and the CLI refuse a file that sets a password |
| L6 | The CLI saved a backup under any name the device sent (a dotfile, a script) | Only a plain name with the expected extension, else the default. CLI test |
| L7 | The page could be framed by another site, for a tricked click | `X-Frame-Options: DENY`, `frame-ancestors 'none'` |
| I1 | What D11 implies: anyone with the password can install any firmware, which could also write the OTP | Documented in D19 and the README |
| I2 | Repeated stores could wear the record sectors | A store that changes nothing writes nothing; the rest is accepted (it needs the password) |
| I3 | A cut-off blob could leave a Wi-Fi area that looks whole | The Wi-Fi area's first sector, then the image's, programmed last of all. Host test |
| I4 | The POST answer files (`/.api`, …) answered a plain GET | Served only right after a POST |
| I5 | The DHCP lease expiry shifted a 16-bit value as `int` | A 32-bit cast |

**Code review of phase 5** (a fresh Opus 5.5 reviewer, 2026-11-11):
- **Overall:** no high findings; one medium and one low, both fixed. Nothing else was found in the flash writes, httpd's POST paths, the DHCP changes, memory or the release workflow.
- **Re-test before the merge:** in the console, build `1.0.0` (`9b8a107`): passed; then the release build, with step 5.2's page changes (`c468ee9`, `527823a`): passed. Both are in [phase5-results.md](research/phase5-results.md).

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Medium:** the CLI's `payload convert --uf2` wrote any payload size. Over 1.5 MB, the UF2's last blocks would overwrite firmware slot A in a USB install | Fixed in the CLI (`a5cb225`), as `tools/mkuf2.py` already refused it. CLI test |
| 2 | **Low:** after a POST cut short, the next GET of `/.api` could read its error answer | Fixed: the answer is offered only after a POST whose body all came. The owner chose to fix it before the release |

**For later releases:**
- **A review fix is re-tested on the Pico along every path it touches,** not only by the host tests. Both bugs of the acceptance came from review fixes that passed the host tests, on paths no re-test had run: a payload upload and a delete (phase 4), and a client joined for 10 minutes (phase 5).
- **Flash is programmed from RAM only** (`storage.h`): flash can't be read while it's being programmed, and the host tests' simulated flash can't show it. Taken up in release 1.0.2 (below), as a copy through a RAM page.
- **D43**, a loader stub for ordinary `.dol` files, the owner's choice in phase 4. Taken up in release 1.1.0 (below).
- **Larger ordinary `.dol` files:** D43's payload holds the program as it is, so a slot's 1.5 MB is the limit, and GCMM's 3.8 MB `.dol` doesn't fit. A stub that unpacks, as gekkoboot's and Swiss's xz packers do, would fit most homebrew. An option.
- **A tidy end to the injection,** the owner's choice after the release. `injection_finish()` puts the clock back to 150 MHz and stops pio0's two state machines, but pio1's two CS counters keep running until power-off, and the bus priority stays on "DMA first". Both are harmless: the counters only read pins, and Wi-Fi was tested this way. The fix stops the counters and resets the priority. The counters could instead stay on, to notice the console reading U10 again (a reset). Re-test on the Pico: a console boot and Wi-Fi. Taken up in release 1.0.1 (below), with DATA's input off too.
- **A wiring-quality number,** the owner's choice after the CS timing series ([cs-timing-results.md](research/cs-timing-results.md), D44): the shortest CS-high gap of each boot, in the boot log and on the Diagnostics tab, with a warning when it's shorter than the console's real gaps. Clean wiring read exactly 136 ns on both consoles; noise on CS would show as a shorter gap before it makes a boot fail. With it, the README's wiring advice from upstream's troubleshooting page (under about 10 cm, 26–28 AWG stranded, the ground at U10).
- **A fourth wire: the console's data to U10,** an option the owner has. The owner's CubeSB overlay board for DOL-001 (PicoBoot-DX, `CubeSB_DOL-001`) brings U10's EXI-IN (pin 6) out on pad 4, marked "GPX". A wire from there to a free Pico input (not GP4–GP7) would show each transfer's command: read or write, and the address. The Pico must only read it, since the console drives it. Uses:
  - starting the injection on the IPL's address, instead of counting 224 CS pulses;
  - a channel from the console to the Pico: a boot menu, homebrew over Wi-Fi, debug output;
  - the fourth wire a U10 replacement would need.

  The first step is a passive build that records the commands and their timing.

## Release 1.0.1: a tidy end to the injection

**Branch:** `release-1.0.1`. **Research:** [injection-end.md](research/injection-end.md). **Done when:** the focused re-test passes on the release build, and the tag's draft matches it.

**The owner's choices (2026-11-14):** the tidy end with DATA's input off, and a focused re-test. The other later-releases items aren't in 1.0.1. The design doesn't change: D31 already says the counter runs "until the injection ends".

| # | Step | Who | Done when |
|---|---|---|---|
| R1 | **The code:**<br>• `counters_stop()` right after the counters' snapshot, in `inject()`;<br>• in `injection_finish()`, the bus priority back to 0 once the DMA is done, and DATA's input off once the state machines are disabled | Opus | Host tests, the layout check and CI pass |
| R2 | **Version and notes:** `CMakeLists.txt` at `1.0.1`, the release notes in `docs/releases/1.0.1.md`. As in phase 5, every build reads `1.0.1`, so builds are named by their SHA-256 | Opus | The owner approved the notes |
| R3 | **Code review** of the branch | Fresh Opus 5.5 reviewer | Findings fixed, or accepted by the owner |
| R4 | **Focused re-test** on the DOL-001, on the build after R3:<br>• update over Wi-Fi from 1.0.0;<br>• 10 cold console boots, each complete, with its margin;<br>• on USB power without the console: "no console seen", Wi-Fi up;<br>• the page up;<br>• optional, with a multimeter: the DATA line's idle voltage in a game, on 1.0.0 and on 1.0.1 | Owner | Passed; results in `docs/research/release-1.0.1-results.md` |
| R5 | **Merge and tag `v1.0.1`:** a merge commit into `main`; the push and the tag on the owner's word; the workflow's draft checked against R4's build, then published | Owner | Released |

**As built (2026-11-14):** R1 in `c6187ad`; R2 in `5541d0b`, and the owner approved the release notes; R3's fixes in `f6aa5e8`, which left the build byte-identical. R4 passed on that build: the update over Wi-Fi from 1.0.0, 10 cold boots, all complete with a margin of 165.7–166.1 ms (mean 165.9 ms, 1.0.0's with the same payload), the trial kept, and "no console seen" on USB power with Wi-Fi up. Results and SHA-256s in [release-1.0.1-results.md](research/release-1.0.1-results.md). R5: merged into `main` on 2026-11-14; the owner had it pushed and tagged `v1.0.1`.

**Code review of release 1.0.1** (a fresh Opus 5.5 reviewer, 2026-11-14):
- **Overall:** no defects in the code. Checked against SDK 2.3.1: the three register writes touch nothing else, and their order is safe on every path through `injection_finish()`. Nothing after them reads DATA, the counters or the bus priority. Wi-Fi's PIO, DMA and clock aren't affected. After its input goes off, the DATA pad is in its reset state, as after every warm reboot of 1.0.0.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Medium:** R4 compared a "DMA 64 KB" figure that only test builds show, on their `/test` page; the Diagnostics tab doesn't have it | Dropped from R4, the owner's choice: the priority reset comes after the console has its payload, and the write has no side effect |
| 2 | **Low:** the research note called E9's 2.2 V a phase 0 measurement; it's the erratum's figure | Fixed in the note |
| 3 | **Low:** no test covers the new steps: the host tests don't compile `counters.c` or `injection.c`, and the PIO simulator doesn't model `counters_stop()` | Accepted by the owner: three register writes, checked against the SDK by the review |
| 4 | **Note:** the code comment called priority 0 "the SDK's"; it's the register's reset value, and the SDK never sets it | Fixed |
| 5 | **Note:** starting the counters again later also needs their IRQ flags cleared | Added to the note |
| 6 | **Note:** R4's incomplete path is the no-console case, which runs the same new lines as a stall | No change |

## Release 1.0.2: flash programmed from RAM only

**Branch:** `release-1.0.2`. **Research:** [flash-from-ram.md](research/flash-from-ram.md). **Done when:** the re-test passes on the release build, and the tag's draft matches it.

**The owner's choices (2026-11-15):** data that isn't in RAM is copied through a 256-byte RAM page; the zeros go back to flash, so that a payload upload or delete goes through the copy; a re-test of every write path (later-releases item 1). The other later-releases items aren't in 1.0.2. The design doesn't change: C6 already says that XIP reads fault while flash is written, and the copy is how `storage.c` keeps to it.

| # | Step | Who | Done when |
|---|---|---|---|
| R1 | **The code:**<br>• in `program()` (`storage.c`), data wholly in SRAM (`SRAM_BASE`–`SRAM_END`) is programmed as now. Other data is copied a page at a time into a 256-byte RAM buffer before interrupts go off, then programmed from it;<br>• `invalidate()` (`payloads.c`) programs its zeros from `static const uint8_t zeros[256]` again, as before phase 5's fix, so they're in flash. Net RAM: the same (the RAM zeros go, the page buffer comes);<br>• the comments in `storage.h` and `payloads.c` updated | Opus | Host tests, the layout check and CI pass. In the build (`arm-none-eabi-nm`), `zeros` is in flash and the page buffer in RAM |
| R2 | **Version and notes:** `CMakeLists.txt` at `1.0.2`, the release notes in `docs/releases/1.0.2.md`. As before, every build reads `1.0.2`, so builds are named by their SHA-256 | Opus | The owner approved the notes |
| R3 | **Code review** of the branch | Fresh Opus 5.5 reviewer | Findings fixed, or accepted by the owner |
| R4 | **Re-test of every write path** on the DOL-001, on the build after R3:<br>• update over Wi-Fi from 1.0.1; a cold console boot keeps the trial (1.0.1's update writer, 1.0.2's boot control);<br>• the same 1.0.2 update again over Wi-Fi, from 1.0.2 into the other slot; a cold boot keeps it (1.0.2's update writer);<br>• a payload upload, then 3 cold boots of that payload (the copy, the payload writer, the boot log);<br>• a settings save, for example the Tx power, read back after a cold boot, then set back (the record writer);<br>• a payload delete, with the built-in chosen first (a delete is refused while that payload is the choice): the Payloads tab shows the slot empty, then a cold boot of the built-in gekkoboot, and the slot still empty (the copy again);<br>• every boot complete, with its margin. No watchdog reset during the upload or the delete: the uptime on the Status tab keeps counting;<br>• the page up, and the Diagnostics tab's boot log showing each boot | Owner | Passed; results in `docs/research/release-1.0.2-results.md` |
| R5 | **Merge and tag `v1.0.2`:** a merge commit into `main`; the push and the tag on the owner's word; the workflow's draft checked against R4's build, then published | Owner | Released |

**As built (2026-11-15):** R1 in `7b1678a`, and its CI run passed; R2 in `52cb416`, and the owner approved the release notes, then their rewording after R3; R3's fixes in `405fc62`, which changed no code. R4 passed on that build:
- both updates over Wi-Fi, from 1.0.1 and then 1.0.2 from 1.0.2, each kept by a power-on;
- Swiss uploaded from the page, then deleted, with no reset either time;
- the Tx power saved and read back after a cold boot;
- 7 power-ons, all complete, with margins of 160.2–164.2 ms. That is within the console's spread: the firmware's start-up is unchanged (389 µs).

Results and SHA-256s in [release-1.0.2-results.md](research/release-1.0.2-results.md). R5: merged into `main` on 2026-11-15; the owner had it pushed and tagged `v1.0.2`.

**Code review of release 1.0.2** (a fresh Opus 5.5 reviewer, 2026-11-15):
- **Overall:** no defects in the code. The RAM path's instructions are the same as 1.0.1's, and `zeros` is in flash (`.rodata`), so uploads and deletes go through the copy. Checked: every writer's buffer and length, the SRAM range (the scratch banks and the stack inside it), no interrupt handlers or second core to re-enter `program()`, and nothing read from flash between interrupts off and XIP off besides `program()`'s own code, as in 1.0.1. The SDK's flash code is byte-identical to 1.0.1's.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Medium:** R4's update from 1.0.1 runs 1.0.1's update writer, not 1.0.2's: an update is written by the firmware that's running | Added to R4, the owner's choice: the same 1.0.2 update again, from 1.0.2 into the other slot |
| 2 | **Low:** R4's delete step didn't show that the delete worked: the built-in gekkoboot boots either way | Added to R4, the owner's choice: the Payloads tab shows the slot empty |
| 3 | **Low:** the release notes could read as if 1.0.1 hangs | Reworded, the owner's choice: 1.0.1 isn't affected, and the update is optional |
| 4 | **Low:** the research note quoted the datasheet's "between program/erase operations" for the ROM's state between its status polls; the ROM's own comment says it | Fixed in the note |
| 5 | **Low:** the note said updates and payloads are checked by their SHA-256 at boot; it's only during an upload, and what protects them is that the header's sector is written last | Fixed in the note |
| 6 | **Note:** the vector table is in RAM, so the second fault is the handler's first instruction; if the handler were cached, it would reboot at once | Added to the note |
| 7 | **Note:** `from + length <= SRAM_END` could wrap only for a pointer just below `0xFFFFFFFF`, which no buffer can be | No change, the owner's choice |
| 8 | **Note:** the copy loop assumes whole pages, as the ROM does on the RAM path; every caller passes whole pages. Its second and later pages never run in 1.0.2 (the only copy is one page) | No change; checked by reading |

## Release 1.0.3: the official gekkoboot r9.4

**Branch:** `release-1.0.3`. **Research:** [gekkoboot-dol.md](research/gekkoboot-dol.md). **Done when:** the re-test passes on the release build, and the tag's draft matches it.

**The owner's choices (2026-11-15):**
- the built-in gekkoboot becomes the DOL rebuilt from the r9.4 release;
- a tool that rebuilds it, and a local test that ties it to the release;
- a focused re-test.

The other later-releases items aren't in 1.0.3. The design doesn't change: it names the built-in gekkoboot, not a build of it.

| # | Step | Who | Done when |
|---|---|---|---|
| R1 | **The code:**<br>• `tools/gekkoboot_dol.py` rebuilds a DOL from a gekkoboot release's `apploader.img`. The stub section runs up to the xz stream, and the data section holds the rest, at `0x81300000`, with that entry. The BSS comes from the packer's `crt0`, checked against the image's end. It refuses an image that doesn't fit that layout;<br>• `assets/ipl/gekkoboot_r9_4.dol` replaced by its output for the r9.4 release (SHA-256 `6172f4a8…`);<br>• tests: the tool on made-up images (layout, BSS, refusals); with `PICOBOOT_PAYLOAD_DIR` (local, like the other payload tests), the asset's payload equals `gekkoboot_pico.uf2`'s, and the tool rebuilds the asset from the release's `apploader.img`;<br>• `GOLDEN_IPL_H_SHA256` in `tests/test_process_ipl.py` updated on purpose, with its comment | Opus | Host tests, the layout check and CI pass. In the build, the built-in IPL is the same size as before. gekkoboot's own `dol2ipl.py` turns the asset into the release's three files byte for byte (recorded in the research note) |
| R2 | **Version and notes:** `CMakeLists.txt` at `1.0.3`, the release notes in `docs/releases/1.0.3.md`. As before, every build reads `1.0.3`, so builds are named by their SHA-256 | Opus | The owner approved the notes |
| R3 | **Code review** of the branch | Fresh Opus 5.5 reviewer | Findings fixed, or accepted by the owner |
| R4 | **Focused re-test** on the DOL-001, on the build after R3:<br>• on 1.0.2 first, with the built-in chosen: a power-on holding D-Pad Down shows "gekkoboot undefined";<br>• update over Wi-Fi from 1.0.2; a power-on keeps it;<br>• 5 cold boots of the built-in, each complete, with its margin. One of them holds D-Pad Down and shows "gekkoboot r9.4";<br>• the built-in gekkoboot starts `ipl.dol` (Swiss) from the SD2SP2;<br>• the page up | Owner | Passed; results in `docs/research/release-1.0.3-results.md` |
| R5 | **Merge and tag `v1.0.3`:** a merge commit into `main`; the push and the tag on the owner's word; the workflow's draft checked against R4's build, then published | Owner | Released |

**As built (2026-11-15):** R1 in `eb29622`, and its CI run passed; R2 in `1c4d903`, and the owner approved the release notes, then one sentence reworded after R3; R3's fixes in `59cbc84`, which changed no firmware code. R4 passed on that build:
- on 1.0.2, the old built-in showed "gekkoboot undefined";
- the update over Wi-Fi from 1.0.2, from the Firmware tab, kept by a power-on;
- 9 power-ons of the built-in, all complete, with margins of 165.4–166.0 ms (mean 165.6 ms, 1.0.1's with a built-in of the same size). One showed "gekkoboot r9.4" with D-Pad Down, and the built-in started Swiss from the SD2SP2.

Results and SHA-256s in [release-1.0.3-results.md](research/release-1.0.3-results.md). R5: merged into `main` on 2026-11-15; the owner had it pushed and tagged `v1.0.3`.

**Code review of release 1.0.3** (a fresh Opus 5.5 reviewer, 2026-11-15):
- **Overall:** no defects in the code. Checked:
  - the release zips, downloaded again, match the files used;
  - the tool rebuilds the asset byte for byte from r9.4's `apploader.img`, and r9.3's files from r9.3's;
  - the header's layout, crt0's opcodes, r13 and the `.sbss` (`0x8130EC60`, 0x7820 bytes, the image's end);
  - the packer clears its own `.sbss` (`__eabi`, then `memzero`), so the BSS that `process_ipl.py` ignores is cleared as before;
  - the built-in IPL in the build is 61,440 B, as before, and `ipl.h` matches a fresh conversion of the asset.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Low:** nothing in CI tied the asset's header to the tool or to its SHA-256. `process_ipl.py` ignores the BSS and the section split, so the golden `ipl.h` doesn't cover them | Fixed: a test in CI checks the asset's SHA-256, and that the tool rebuilds it from its own image |
| 2 | **Low:** `ecosystem.md` said upstream's PIO only changed its copyright year since 2022; `e9f420b` (2024) reworked the output side | Fixed in the note. The CS count only had its pins renamed, so the conclusion stands |
| 3 | **Low:** the research note called the code-layout difference mostly branch offsets; most of it is the same code in a different place | Fixed in the note |
| 4 | **Low:** the note said three release files confirm the rebuild. With the IPL ROM gekkoboot's CI fetches, the Qoob Pro pair do too, and the tool rebuilds the old CI asset (`elf2dol`'s output) from its own image | Fixed in the note: five files, and the `elf2dol` header match |
| 5 | **Low:** the note said the zip was uploaded two days after the release was created; that date is the tagged commit's, and the upload came 40 s after publishing | Fixed in the note |
| 6 | **Note:** gaps in the tool's tests: an empty `.sbss`, crt0 without its r13 set-up, and a regex that matched two messages | Fixed: two tests, and the regex tightened. Each new test fails when its check is taken out of the tool |
| 7 | **Note:** the release test's docstring gave the release's path for `apploader.img`; the test reads it from the folder's top | Fixed |
| 8 | **Note:** the note didn't say how its 16-byte shares were counted | Fixed: windows at 4-byte steps, which give 94.8% and 39.1% |
| 9 | **Note:** the tool refuses a packer linked at `0x81300000`, a missing input gave a traceback, and the xz magic isn't checked further | The missing input fixed, with a test. The other two kept: a refusal is the tool's answer to a layout it doesn't know, and it copies the image as it is, so a false match can't change what the Pico sends |
| 10 | **Note:** "stub" also names gekkoboot's `stub/` | "The packer" in the note |
| 11 | **Note:** the release notes said the built-in "makes" the release's UF2, and "Tested on a DOL-001" came before R4 | Reworded, with the owner's approval: gekkoboot's own tool turns it into the release's UF2. "Tested" kept, as R4 makes it true before publishing |

## Release 1.1.0: ordinary `.dol` files (D43)

**Branch:** `release-1.1.0`. **Research:** [dol-loader.md](research/dol-loader.md). **Design:** D43 (v1.8). **Done when:** the console test passes on the release build, and the tag's draft matches it.

**The owner's choices (2026-11-16):**
- the stub starts like gekkoboot's packer: real mode, the hardware quiesced, DABR and IABR cleared;
- low memory filled as gekkoboot's `__SYS_PreInit` does, the video mode left 0;
- a `.dol` that reaches the payload image or low memory is refused;
- console tests with cubiboot's `ipl.dol`, Swiss r2119's `.dol`, and GCMM or the 240p Test Suite, with cold boots.

The other later-releases items aren't in 1.1.0.

| # | Step | Who | Done when |
|---|---|---|---|
| R1 | **The stub:**<br>• `src/dolstub/dolstub.S`, built for `0x81300000` with Ubuntu's `binutils-powerpc-linux-gnu`, added to `docker/Dockerfile`. It does D43's four steps: the start, low memory, the program, the jump;<br>• the table after it: the entry, the BSS, and each section's address, offset and size. Its format is in the source's header;<br>• `tests/ppcsim.py`: the PowerPC interpreter from the research, extended with `rfi`, `mfspr`/`mtspr`, `mftb` and the hardware registers the stub touches, which read back test values;<br>• `tests/test_dolstub.py`. It runs the stub on made-up `.dol` files and checks:<br>&nbsp;&nbsp;– the sections, the BSS and low memory;<br>&nbsp;&nbsp;– every line written back and the instruction cache invalidated before the jump;<br>&nbsp;&nbsp;– no stores outside the program, low memory and the hardware registers;<br>&nbsp;&nbsp;– the jump to the entry.<br>It also assembles the source where binutils is installed (CI, Docker), and checks the bytes against the converters' copy. With `PICOBOOT_PAYLOAD_DIR`, it runs cubiboot's `ipl.dol` and Swiss r2119's `.dol` too | Opus | The tests and CI pass, and CI's bytes equal the local build's. The stub's size is recorded |
| R2 | **The converters:**<br>• `src/web/dol.js` and `tools/picoboot-wifi.py`: an IPL build as now; any other `.dol` gets the stub, the table and its sections;<br>• both refuse a section or BSS that reaches the payload image or low memory, or lies outside main memory, with a message that says which. The slot-size check stays as it is;<br>• the page says which kind it converted;<br>• `tools/process_ipl.py` (the built-in) is unchanged;<br>• tests: the CLI's converter, its refusals, and the interpreter on its output; the Node test checks that `dol.js` gives the CLI's bytes;<br>• the README and the page's help text name both kinds | Sonnet, in a worktree (as for 4.5–4.7), Opus reviews | The tests and CI pass. For cubiboot's `ipl.dol` and Swiss r2119's `.dol`, the page and the CLI give the same payload, and the interpreter runs each to its entry |
| R3 | **Version and notes:** `CMakeLists.txt` at `1.1.0`, the release notes in `docs/releases/1.1.0.md` | Opus | The owner approved the notes |
| R4 | **Code review** of the branch | Fresh Opus 5.5 reviewer | Findings fixed, or accepted by the owner |
| R5 | **Console test** on the DOL-001, on the build after R4:<br>• update over Wi-Fi from 1.0.3; a power-on keeps it;<br>• three `.dol` files converted by the page, at least one also by the CLI: cubiboot's `ipl.dol`, Swiss r2119's `.dol`, and GCMM or the 240p Test Suite;<br>• for each, 3 cold boots after a long power-off, and a quick power cycle. cubiboot starts Swiss from the card, Swiss shows the card, GCMM or the 240p Test Suite runs with the controller;<br>• an IPL build (Swiss in payload 1) still boots;<br>• every boot complete, with its margin; the page up | Owner | Passed; results in `docs/research/release-1.1.0-results.md` |
| R6 | **Merge and tag `v1.1.0`:** a merge commit into `main`; the push and the tag on the owner's word; the workflow's draft checked against R5's build, then published | Owner | Released |

**As built (2026-11-16):**
- **The steps:**
  - R1 in `7edaaf3`. Its CI run passed, and CI's stub bytes equal the local build's.
  - R2 in `00ae8c7`, by a Sonnet agent in a worktree, reviewed. Its CI run passed.
  - R3 in `a83ac7c`. The owner approved the notes, then their rewording after R4.
  - R4's fixes in `3713e8d`.
  - After R5, the CLI's payload line in `79f6879`.
- **R5 passed on R4's build:**
  - the update over Wi-Fi from 1.0.3, kept by a power-on;
  - cubiboot's `ipl.dol`, gekkoboot's lowmem `.dol` and Swiss r2119's `.dol`, each from the page, each with 3 cold boots and a quick power cycle;
  - cubiboot once more, from the CLI, with a cold boot;
  - Swiss's IPL build still booting;
  - 14 power-ons, all complete, with margins of 158.2–163.8 ms.

Results and SHA-256s in [release-1.1.0-results.md](research/release-1.1.0-results.md). R6: merged into `main` on 2026-11-16; the owner had it pushed and tagged `v1.1.0`.

**Differences from the plan:**
- **The third `.dol` is gekkoboot's lowmem build** (the owner's choice). GCMM's GameCube `.dol` (3.8 MB) and the 240p Test Suite's don't fit a slot.
- **R1 added `tools/dolstub.py`.** It wraps an ordinary `.dol` as a `.dol` for `0x81300000`, so the existing IPL converters take it unchanged; the page and the CLI carry copies.
- **The stub's bytes are checked in** as `src/dolstub/dolstub.bin`. CI's build must match them, and the converters' copies are tested against them.
- **The stub is assembled with `-m603`:** binutils' `-m750cl` lacks the 750's `mftb`.
- **It zeroes the BSS before the sections,** so a section inside the BSS's range keeps its data.
- **R4 put `rfi` at the system-call vector** (design v1.9).

**Code review of release 1.1.0** (a fresh Opus 5.5 reviewer, 2026-11-16):
- **Overall:** nothing that would hang or corrupt anything on the paths R5 exercises.
  - The stub matches gekkoboot's packer and `__SYS_PreInit` instruction for instruction where it says so.
  - libogc2, devkitPro libogc 2.14.1 and libogc 3 all cope with its real-mode entry at a physical address.
  - The CLI, the page and the reference gave the same bytes and messages for 2,000 random `.dol` files.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Risk on hardware:** `b .` at the system-call vector. libogc's cache flushes end in `sc`, so a program that flushes before it sets up its own handlers (in a `__SYS_PreInit`) would hang. From gekkoboot (`rfi` at every vector) or Swiss (`rfi` at `0xC00`) it boots | Fixed: `rfi` at `0xC00`; the other vectors still loop |
| 2 | **Risk on hardware:** the memory size is read as one 32-bit word of a 16-bit register | Accepted by the owner. Dolphin models such a read as two registers, as the stub assumes, and gekkoboot's packer relies on the same for the DSP's. R5's boots ran normally |
| 3 | **Test gap:** no test that a section inside the BSS keeps its data; a stub clearing the BSS last passed | Fixed: a test, which that stub now fails |
| 4 | **Test gap:** the memory controller's value wasn't checked to be masked to 3 bits | Fixed: garbage in the other bits |
| 5 | **Doc error:** the release notes said the loader sets up what the console's menu would, and named only sections among what's refused | Fixed: what gekkoboot fills in; "sections or BSS" |
| 6 | **Doc error:** the research note missed Swiss's `rfi` at `0xC00`, and had a payload size and `-m750cl` wrong | Fixed |
| 7 | **Note:** a section outside main memory got a message about the payload image or low memory | Fixed: an "outside main memory" check first, in all three converters |
| 8 | **Note:** a `.dol` too big for a slot converts, and only the Pico refuses it | Accepted by the owner: as for IPL builds; the slot check stays where it is |
| 9 | **Note:** D43, D16 and the README's wording | Fixed: design draft v1.9 |
| 10 | **Note:** the interpreter ignored record and link forms the stub doesn't use, and the time base's carry was untested | Fixed: they halt the run; a test with a low word of 0 |
| 11 | **Note:** `tools/dolstub.py` on its own can name a different first fault than the converters, for a file with several | Accepted by the owner: every converter refuses such a file |

## Release 1.1.1: no CS pull-up

**Branch:** `release-1.1.1`. **Research:** [cs-pull-up.md](research/cs-pull-up.md). **Design:** D36 (v1.10). **Done when:** the console test passes on the release build, and the tag's draft matches it.

**The owner's choices (2026-11-20):**
- CS is left as reset leaves it; D36's clock-qualified count stays;
- the CS test mode goes, all of it;
- the README asks for a short ground wire near U10, and says how the CS counts show a wiring problem;
- a handoff note for PicoBoot-DX;
- the console test on the second console only, 20 boots (the dev console's 51-boot series counts for it).

The other later-releases items aren't in 1.1.1.

| # | Step | Who | Done when |
|---|---|---|---|
| R1 | **The code and docs:**<br>• `injection_pio.c` no longer pulls CS up. The `cs_pullup` parameter goes from `on_transfer_program_init()`, `injection_prepare()` and `inject()`;<br>• the CS test mode goes: the test page's line and its two actions (`page.c`), `main.c`'s reading of boot control's bit 7, and the Diagnostics tab's "CS pull-down test" row;<br>• boot control's bit 7 (`BOOTCTL_FLAG_TEST_CS_PULLDOWN`) goes. A test build may have left it set, so the owner clears it on both Picos' test page ("CS pull-up") before they leave the test build, and a later flag can use the bit;<br>• the boot report's `cs_pulldown_test` goes, from the API and `docs/api.md` too, with `api` still 1: the field only ever meant something in test builds. It's the report's last field, so the boot log's size (60 bytes on the Pico, 68 on the host) and offsets don't change, and its entries stay readable without a format bump;<br>• the PIO simulator's setup and the tests follow;<br>• the README's install notes: a short ground wire from the Pico's GND to a ground point near U10, and the boot log's CS counts (285 for the built-in, 1544 for Swiss) as the check;<br>• `docs/handoff/picoboot-dx-cs-pull-up.md`: the finding, the evidence from both consoles, and the change for PicoBoot-DX | Opus | Host tests, the PIO simulator, the layout check and CI pass |
| R2 | **Version and notes:** `CMakeLists.txt` at `1.1.1`, the release notes in `docs/releases/1.1.1.md` | Opus | The owner approved the notes |
| R3 | **Code review** of the branch | Fresh Opus 5.5 reviewer | Findings fixed, or accepted by the owner |
| R4 | **Console test** on the second console, on the build after R3:<br>• on both Picos, while still on the test build: "CS pull-up" on the test page, to clear bit 7;<br>• 1.1.0 first, then the update over Wi-Fi to 1.1.1; a power-on keeps it;<br>• 20 cold boots, each starting its payload: Swiss (an IPL build) and the built-in gekkoboot, each count exact (1544 and 285), or with a glitch that the injection survived;<br>• the page up | Owner | Passed; results in `docs/research/release-1.1.1-results.md` |
| R5 | **Merge and tag `v1.1.1`:** a merge commit into `main`; the push and the tag on the owner's word; the workflow's draft checked against R4's build, then published | Owner | Released |

**As built (2026-11-20):**
- **The steps:**
  - R1 in `0219553`. Its CI run passed.
  - R2 in `e9c145b`. The owner approved the notes.
  - R3 in `b7dc50b`: the code review and its fixes.
  - After R4, in `98af5a1`: the PIO simulator refuses `jmp pin` without a JMP_PIN, as PicoBoot-DX `0ed9e8b` does (the owner's choice). Tests only; no result changes.
  - In `61677af`: the research note [cs-at-start-up.md](research/cs-at-start-up.md), and the notes it corrects. Docs only.
  - CI passed on every push of the branch, the last on `61677af`. A clean build of it gives R4's files byte for byte.
- **R4 passed on R3's build,** on the second console:
  - bit 7 cleared on the test page first;
  - 1.1.0 over Wi-Fi, then the update to 1.1.1, kept by a power-on;
  - 21 cold boots, each starting its payload: Swiss on 16, the built-in gekkoboot on 5, every count exact, no glitch;
  - margins of 159.4–160.1 ms;
  - the page up.

Results and SHA-256s in [release-1.1.1-results.md](research/release-1.1.1-results.md). R5: merged into `main` on 2026-11-20; the owner had it pushed and tagged `v1.1.1`.

**Differences from the plan:**
- **21 cold boots, not 20:** the trial's power-on, and 20 after it.
- **The update to 1.1.1 came while 1.1.0 was still on trial,** before any power-on. It worked.
- **Bit 7 on the dev console's Pico,** which ran its series in pull-down mode, was cleared on its test page after the test (the owner).
- **The README's check is "225 more than the payload's transfers"** (R3), not the two counts R1 names.
- **One more pull-up failure:** boot 29, the test build's last, showed the console's own menu with 635 CS transfers (285 expected). The cause of so many isn't known.

**Code review of release 1.1.1** (a fresh Opus 5.5 reviewer, 2026-11-20):
- **Overall:** no bug in the firmware; the removal is complete.
  - The boot report is 60 bytes in the Pico's build before and after, with every offset the same (from the ELF's debug info). An old entry's set byte is now unread padding. R4 read the test build's entries in 1.1.1.
  - Bit 7 is ignored everywhere: only "original IPL once" is ever tested.
  - Nothing needs CS high at start: each program waits for CS high first.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Risk on hardware, later:** 1.1.1 ignores bit 7 but never clears it, an update over Wi-Fi keeps it, and the plan freed the bit for a later flag. The dev console's Pico has it set | Fixed in the plan (the owner's choice): the owner clears it on both Picos' test page before they leave the test build. Done on both: the second console's in R4, the dev console's after it |
| 2 | **Test gap:** the simulated console started CS high, so no test covered CS low at the start, now the normal start on the dev console | Fixed: `cs_low_until` in `tests/pio_sim.py`. The injection and the counters are tested with CS low, then high, with and without a clockless glitch. PicoBoot-DX v0.10's count fails the glitch case. The handoff names the gap for PicoBoot-DX |
| 3 | **Doc error:** the release notes promised that a console booting with 1.1.0 boots with 1.1.1, on two consoles' evidence. Without the pull-up, D36's known limit is unguarded | Fixed (the owner's choice): "both test consoles boot every time", the promise dropped. D36 says the limit is unguarded |
| 4 | **Doc error:** `docs/api.md`'s rule says `api` counts up when a field goes away | Fixed: a field only test builds set can go without it |
| 5 | **Note:** two comments still described the CS test mode | Fixed |
| 6 | **Test gap:** nothing pinned the boot report's size in the Pico's build, and the plan quoted the host's 68 bytes | Fixed: an assert at 60 bytes in `storage.c`; the plan gives both sizes |
| 7 | **Doc error:** "tens of thousands" CS edges, where most were hundreds of thousands; 1544 holds for one Swiss build only; the noise table had no payload column | Fixed: "tens or hundreds of thousands"; "225 more than the payload's transfers"; the column |
| 8 | **Doc error:** inferences stated as fact in the research and handoff notes | Fixed: marked [inf]; RP2350-E9 named as a possible cause of CS reading high |
| 9 | **Note:** the test page printed "LOW" for CS at "PIO ready", now a normal reading | Fixed: "low" |
| 10 | **Note:** the plan's header named design v1.7 | Fixed: v1.10 |

## Diagnostic build: CS timing (D44)

**Branch:** `diag-cs-timing`. **Research:** [cs-timing-diagnostics.md](research/cs-timing-diagnostics.md). **Design:** D44 (v1.13). **Done when:** the console series ran on both consoles, its results are written up, and the owner has decided what follows.

**The owner's choices (2026-11-22):**
- a summary per boot in the test builds' boot log, plus the current boot's raw record as a download;
- CLK's level recorded at each falling CS edge;
- clean setups only, no deliberately poor wiring;
- a test build first; what goes into a release is decided from its numbers.

| # | Step | Who | Done when |
|---|---|---|---|
| T1 | **The code, test builds only:**<br>• `cs_timing` in its own `pio/timing.pio` (10 instructions; pio1 then holds 20 of its 32), its set-up, start and stop with the counters, and a DMA channel into a 32 KB buffer in `.uninitialized_data`;<br>• the summary as a pure C function, after the injection and before Wi-Fi;<br>• the test builds' boot report with the summary, under its own log version;<br>• the summary in `/api/diagnostics`, the raw record at `/api/test/cs-timing`, and the summary on the test page;<br>• PIO simulator tests of the recorder: known transfers and gaps, a low CS start, a clockless glitch, a short CS-high blip inside a transfer, and CLK high at a CS fall; host tests of the summary | Opus | Host tests, the PIO simulator, the layout check and CI pass. A release build of the branch is byte for byte v1.1.1's (update `714140b8…`, UF2 `cfaed293…`) |
| T2 | **Code review** of the branch | Fresh Opus 5.5 reviewer | Findings fixed, or accepted by the owner |
| T3 | **Console series** with the test build `1.1.2-dev`, installed over Wi-Fi on both consoles:<br>• 10 cold boots each, 5 with the built-in and 5 with Swiss;<br>• on the second console, 3 quick power cycles as well;<br>• the boot log read once per console, and the raw record of a few boots downloaded;<br>• afterwards back to 1.1.1 (a downgrade; the log starts afresh) | Owner | The logs and records are in |
| T4 | **Results** in `docs/research/cs-timing-results.md`: the real gaps in BS1 and in the payload, any blips, the CLK bit, and transfers 225 and 226 of the quick power cycles | Opus | The owner approved the note |
| T5 | **The owner decides** what follows: a minimum CS-high gap for D36, a wiring-quality number in the releases, the fourth wire, or nothing; and whether the diagnostic code merges into `main` for later test builds or stays on its branch | Owner | Decided; the plan updated |

**As built (2026-11-22):**
- **The steps:**
  - T1 in `519c7e7`; T2's fixes in `71fdd9c`, with design v1.14 and plan v1.21 (`52d2603`). CI passed on each push.
  - T3: the test build `1.1.2-dev+52d2603`, on both consoles. The dev console: 10 cold boots. The main console: 23, of which 16–18 were quick power cycles.
  - T4 in `4fe5361`: [cs-timing-results.md](research/cs-timing-results.md).
  - T5: the owner's decision, and the short-gap threshold at 100 ns, on the branch.
- **The results:**
  - all 33 power-ons started their payload, with no stall and CLK low at every CS fall;
  - the read's timing is the same on both consoles to the nanosecond;
  - the dev console's glitch is an unclean first rise of CS, within about 20 ns.
- **T5, the owner's decision:**
  - **next:** a wiring-quality number for the releases (later releases, above);
  - **the code stays on `diag-cs-timing`,** not merged into `main`;
  - **both consoles back to 1.1.1.**

**Differences from the plan:**
- **The recorder writes two words per CS low period,** in its own `timing.pio`, so no time wraps around (design v1.14).
- **"Byte for byte v1.1.1" needs the build date pinned:** the SDK's binary info embeds `__DATE__`, so the check builds with `SOURCE_DATE_EPOCH` at v1.1.1's commit.
- **The main console ran 23 boots, not 13:** the owner added some.
- **The short-gap threshold was wrong at 500 ns:** every real gap is shorter. It's 100 ns since T5; the shortest gaps, which the series relied on, were right throughout.

**Code review of T1** (a fresh Opus 5.5 reviewer, 2026-11-22):
- **Overall:** a clean release build of the branch is byte for byte v1.1.1 (with `SOURCE_DATE_EPOCH` at v1.1.1's commit: the SDK's binary info embeds the build date); the recorder can't disturb the injection.

| # | Finding | Where it's handled |
|---|---|---|
| 1 | **Bug:** boots without an injection (warm boots, "original IPL once") logged a summary of zeros, where "nothing measured" was meant; a series' minimum gap would read 0 ns | Fixed: the summary starts as "nothing measured" in `main()` |
| 2 | **Doc error:** D44 and T1 described one word per transfer, a 16 KB buffer and `counters.pio`; the code has two words, 32 KB and `timing.pio` | Fixed: design v1.14 and plan v1.21 drafts |
| 3 | **Risk on hardware, small:** the longer boot reports grew two `.bss` arrays by 3.3 KB, about 0.4 ms of margin, and left 968 bytes of the 24 KB budget | Fixed (the owner's choice): in test builds those rows are in uninitialized RAM |
| 4 | **Test gap:** CI builds releases only, so the test-build code isn't compiled there; an older host test rejects `X.Y.Z-dev+hash` versions | Accepted by the owner: test builds are compiled locally |
| 5 | **Note:** a POST to `/api/test/cs-timing` got 404, not 405 | Fixed |
| 6 | **Doc error:** after any restart of the Pico the download was an empty 200 | Fixed: 409 `not_recorded`; the docs say any restart loses it |
| 7 | **Note:** `timing_stop()` read the DMA count with a write possibly in flight, and aborted without clearing EN first (RP2350-E5) | Fixed: EN cleared, the abort, then the count from the write address |
| 8 | **Note:** the dead-time wording, and the comments on X and Y running out | Fixed: 8–20 ns, pulses near an edge merge; both run-outs unreachable |
| 9 | **Note:** the summary found the undriven start from `cs_high_at_ready`, not from the record | Fixed (the owner's choice): a first period with no CS-high time |
| 10 | **Doc error:** a switch of build doesn't start the boot log afresh at once | Fixed in D44 |
| 11 | **Notes:** the coexistence test can't show the recorder slowing the injection; no sync note for the recorder's set-up; the simulator's MOV to ISR kept the shift count; the test page showed short times as 0.00 us | Fixed: the test's docstring, sync notes, the simulator and a test, ns on the page |

## Local builds and CI

- **Local:** Opus builds in Docker (`docker/Dockerfile`: Ubuntu, SDK 2.3.1, picotool 2.3.1, arm-none-eabi-gcc), the same setup the size research used ([sizes.md](research/sizes.md)). Test UF2s go to the owner from there or from CI.
- **CI:** builds, then runs every test:
  - Python tools;
  - the PIO simulator;
  - host C tests;
  - the layout check;
  - the Node test for the JS converter (from phase 4).

  The CI run is the authority on whether a step is done.
