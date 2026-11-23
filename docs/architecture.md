# PicoBoot-WiFi: architecture

| | |
|---|---|
| **Project** | PicoBoot-WiFi: PicoBoot-DX for the Pico 2 W, with over-the-air firmware and payload updates |
| **Status** | Approved v1.23, 2026-11-24 |
| **Owner** | Remko Kleinjan |
| **Research** | [docs/research/](research/README.md) |

## Status

| Version | Date | What changed and why |
|---|---|---|
| v0.1 | 2026-11-08 | First draft. Builds on the 2026-11-07 OTA research and the owner's layout proposal (slots A/B 256K, payload 1 MB, Wi-Fi 500K). New research (`docs/research/`) changes three things: the payload area must stay 1.5 MB (Swiss r2119 is 1.29 MiB); the Pico 2 W has 4 MB of flash; and a partition table would break USB payload installs, so this draft proposes a loader of our own instead of the boot ROM's A/B. |
| v0.2 | 2026-11-08 | The owner answered four rounds of questions. Fifteen items became decisions (D6, D7, D9, D11, D12, D14–D17, D19–D22, D27, D28). Home Wi-Fi (D21) moves out of the first release and stays an option for later releases, so it leaves the phase plan. New proposals: the license (D29) and the access point's security details (D30). Proposals D4, D5, D8, D10, D13, D18, D23–D26, D29, D30 and the phase plan in D28 wait for approval with the rest of the design. |
| v1.0 | 2026-11-08 | The owner approved v0.2 with all its proposals ("ok, go ahead"). Every proposal is now a decision; nothing else changed. Step 1 of the design cycle is closed; the implementation plan (`docs/implementation-plan.md`) follows. |
| v1.1 | 2026-11-08 | The implementation plan's design points DP1–DP5, approved with the plan, become D31–D35. The owner has a current meter but no oscilloscope or logic analyzer, and one DOL-001, so: the Pico measures its own boot timing (D31, changes D10); a minimal Wi-Fi report page comes early (D32, changes D28); the rail is checked with a stress test instead of a scope (D33, changes D28 phase 0); the DOL-001 is the test console (D34); and there are no pre-releases (D35, changes D24). |
| v1.2 | 2026-11-08 | Approved by the owner. Phase 0 results ([phase0-results.md](research/phase0-results.md)). The owner decided three changes: guard the 224 skipped CS pulses against glitches with a CS pull-up and a clock-qualified count (D36), after 2 blue screens in 56 cold boots were traced to a false CS edge at the console's power-up; keep the last flash sector free, because USB installs write picotool's RP2350-E10 block there (D37, changes D6); and recommend 5 V wiring, because the console's 3.3 V at the Pico is 3.15–3.21 V (D38). Also noted: HAD_POR survives warm reboots, so D8's marker is required (confirmed); the boot margin is about 166 ms (D10); the risks table is updated. |
| v1.3 | 2026-11-08 | Approved by the owner at the end of phase 1. During phase 1, for step 1.10's many console boots, the owner chose a boot log in the loader region (D39): the loader region shrinks to 8K (the loader is 2.7K), and the freed 8K keep up to the last 32 boot reports. Step 1.11 sets two values from the phase 1 console tests ([phase1-results.md](research/phase1-results.md)), as the owner chose: the boot path may add at most 10 ms over PicoBoot-DX (D10; the loader costs 3–5 ms), and the injection keeps 250 MHz with flash divider 3 (D27; 87 console boots, cold and warm, without a fault). The 225th-transfer edge check was proven on hardware (2 power-up glitches survived); the phase 1 code review found one theoretical gap, which the owner chose to accept as a known limit rather than change the proven programs (D36). For approval at the end of phase 1. |
| v1.4 | 2026-11-09 | Approved by the owner at the end of phase 2. Phase 2, step 2.1: the settings, their values and defaults (D40). The owner chose channel 6 as the default and named Tx power levels (low, medium, high, max) instead of a dBm number. Step 2.3: how a settings change takes effect (D41). The owner chose an access-point restart at once, a trial for Wi-Fi changes until a client joins, and a write-only password. Step 2.6: the owner set the Tx power levels to 5, 10 and 15 dBm and the chip's limit (D40), and the stalled-injection LED to blink for as long as Wi-Fi is on (D26). |
| v1.5 | 2026-11-10 | Approved by the owner at the end of phase 3. Phase 3, step 3.1: the update file format ([update-format.md](update-format.md)). The owner chose to always carry the Wi-Fi blob in the release file, which drops D14's copy between the Wi-Fi areas, and to allow downgrades with a warning (D11). The header names the layout and the lowest loader version, since OTA can't update the loader (D11). Step 3.4: D12 as built; the owner chose 3 crashes for the crash loop, and a fallback for good. |
| v1.6 | 2026-11-11 | Approved by the owner at the end of phase 4. Phase 4: payload uploads (D42). The owner chose names they pick (which brings boot control format 2 and loader version 3, one USB install), uploads into payload 2 while the built-in boots, no deleting the chosen payload, and backups without the Wi-Fi password. D17: a USB install is recorded after a complete injection (or a choice, rename or upload over Wi-Fi), so the console's power-on shows it. D43: a loader stub for ordinary `.dol` files, for a later release. The phase 4 review: a wider fingerprint, no update to a firmware from before boot control format 2, Wi-Fi 1.5 s later on an "original IPL once" boot. |
| v1.7 | 2026-11-11 | Approved by the owner at the end of phase 5. Phase 5: how a release is made (D24): a draft release by CI on a version tag, with the CLI and checksums; a release's version without the git hash. The security review (D19): what the password gate means, and DNS-rebinding reads of the GETs accepted by the owner. |
| v1.8 | 2026-11-16 | Approved by the owner. D43, a loader stub for ordinary `.dol` files, for release 1.1.0, from the research in [dol-loader.md](research/dol-loader.md). The owner chose a start like gekkoboot's packer, low memory filled as gekkoboot does, and refusing a `.dol` that reaches the payload image or low memory. D16 and D25 updated to match. |
| v1.9 | 2026-11-16 | Approved by the owner. D43 as built (release 1.1.0): the stub is linked at `0x01300000`; the BSS is zeroed before the sections; the system-call vector returns (`rfi`), from the code review. D16: a `.dol` at `0x81300000` with another entry point is refused as before. |
| v1.10 | 2026-11-20 | Approved by the owner. D36 without the CS pull-up, for release 1.1.1, from the research in [cs-pull-up.md](research/cs-pull-up.md): the pull-up makes a second DOL-001 fail, and the clock-qualified count alone guards the 224 skipped transfers on both consoles. The CS test mode goes with it. |
| v1.11 | 2026-11-20 | Approved by the owner. D36 as built (release 1.1.1), from its code review: without the pull-up, the known limit is unguarded; the expected CS count is 225 more than the payload's transfers; the dev console's series has 48 logged boots. From the console test: the second console booted 21 of 21 cold boots on 1.1.1 ([release-1.1.1-results.md](research/release-1.1.1-results.md)). |
| v1.12 | 2026-11-22 | Approved by the owner. D36's evidence, from the research on the second console ([main-console-tv.md](research/main-console-tv.md)): the pull-up failed on that console's marginal wiring of the time, which the dev console's flex cable never showed, so its harm may need such wiring; and a few extra CS transfers on most boots point at wiring that picks up noise. The decision doesn't change. |
| v1.13 | 2026-11-22 | Approved by the owner. D44, CS timing diagnostics in test builds, from the research in [cs-timing-diagnostics.md](research/cs-timing-diagnostics.md): a passive recorder in pio1 times every CS period and notes CLK at each CS fall; a summary per boot in the test builds' boot log; releases unchanged. The owner chose a per-boot summary in flash, the CLK bit, clean setups only, and a test build first. |
| v1.14 | 2026-11-22 | Approved by the owner. D44 as built (step T1) and after its code review (T2): two words per CS low period in its own `timing.pio`, so no time wraps; a 32 KB record; the test builds' boot-log rows in uninitialized RAM too (the owner's choice); the record shows the undriven start itself (the owner's choice); "nothing measured" on boots without an injection; the dead time; what a switch of build does to the boot log. |
| v1.15 | 2026-11-22 | Approved by the owner. D44 decided (step T5), from the console series in [cs-timing-results.md](research/cs-timing-results.md): the short-gap threshold is 100 ns, as real gaps are 136 ns or more; the code stays on its branch, not in `main` (the owner's choice); a wiring-quality number for the releases follows. |
| v1.16 | 2026-11-22 | Approved by the owner. Release 1.1.2: D45, a noise warning from the CS counts, from the research in [wiring-quality.md](research/wiring-quality.md): the Diagnostics tab compares each boot's count with a clean boot's and warns of noise on CS (the owner chose it over the shortest CS-high gap, which adds little to the counts); D46, the page's status polling one request at a time with a back-off, from the research in [weak-link.md](research/weak-link.md). |
| v1.17 | 2026-11-22 | Approved by the owner. D45 and D46 as built (release 1.1.2, step R1): both rules are in the page's own script, and there's no `rules.js` (the owner's choices). D45's warning is checked by hand, with made-up diagnostics, like the page's other warnings. |
| v1.18 | 2026-11-22 | Approved by the owner. D45 and D46 after release 1.1.2's code review (R3): D45 names boot 108 as a known exception (the wording stays, the owner's choice); D46 sends a request of its own after an action, drops older answers, carries on after an error, and a success brings a longer wait back to 5 s. |
| v1.19 | 2026-11-23 | Approved by the owner. D47: length-based injection, a feasibility stage first, from the research in [length-injection.md](research/length-injection.md). The owner chose a simulator prototype, a CLK capture test build from `diag-cs-timing` (CS and CLK every 4 ns), a length-only test build on its own branch, console series as T3's on clean wiring, the switch point from the simulator, and the name "Injection: standard / length-based". |
| v1.20 | 2026-11-23 | Approved by the owner. D47 after plan step L1, the simulator prototype: the detector and the output program as built (14 and 26 instructions), the switch at bit 66, and L1's finding: a CS blip that splits a payload block makes every later block one bit late, in today's `clocked_output` too. The owner's choice: the prototype waits for CS high before each later block's IRQ, and today's `clocked_output` gets the wait in a release of its own. |
| v1.21 | 2026-11-23 | Approved by the owner. D47's CLK capture after its code review (plan step L3), the owner's choices: it starts at the first rising CLK edge inside a CS low period, its summary also counts BS1's phases by length, and `diag-cs-timing` has release 1.1.2 merged in, with a CI step that compiles a test build. |
| v1.22 | 2026-11-23 | Approved by the owner. D47 after the length test build's code review (plan step L7): one CLK glitch during BS1 starts the length method's payload early, a risk the owner chose to weigh at L9; a failed boot may show a black screen; BS1's clock as measured. |
| v1.23 | 2026-11-24 | Approved by the owner. D47 after its console series (plan step L8, the dev console only) and the owner's decision (L9): release 1.2.0, with the CS wait in today's injection and the length method as an opt-in, the count the default. |

## How to read the decisions

Every decision has a number (D1, D2, …) that stays the same in later versions. Each one is one of three kinds:

- **Decision:** made by the owner.
- **Proposal:** waiting for the owner's approval.
- **Open:** a question that needs an answer, often from a hardware test.

## Summary

| # | Topic | Kind | In short |
|---|---|---|---|
| D1 | Board | Decision | Pico 2 W only |
| D2 | Goal | Decision | PicoBoot-DX's job, plus OTA of firmware and payload |
| D3 | Code base | Decision | New project; PicoBoot-DX code may be reused |
| D4 | Compatibility | Decision | Drop-in for PicoBoot-DX: same wiring, payload format and address |
| D5 | Non-goals | Decision | No other boards, no internet update check, no Bluetooth, no HTTPS |
| D6 | Flash layout | Decision | 256K slots, 2×240K Wi-Fi, payload 1.5 MB at `0x80000`, second payload, all 4 MB |
| D7 | Slot switching | Decision | Own loader with `rom_chain_image()`, no partition table |
| D8 | When to inject | Decision | Only on a real power-on |
| D9 | Wi-Fi start | Decision | Only after the injection has finished |
| D10 | Boot-time budget | Decision | At most 10 ms over PicoBoot-DX before the PIO is ready; the loader path costs 3–5 ms |
| D11 | Update file | Decision | One file with the firmware, optionally the Wi-Fi blob; SHA-256, no signing |
| D12 | Trial and fallback | Decision | On trial until a good power-on boot or *Keep*; a crash falls back |
| D13 | Switching firmware | Decision | Free switching between two good slots |
| D14 | Wi-Fi blob | Decision | Outside the slots, one copy per slot, at fixed addresses |
| D15 | Payload slots | Decision | Two payload slots; you choose which one boots |
| D16 | Payload formats | Decision | Payload UF2 (any family) and raw payload on the device; `.dol` converted in the browser and the CLI |
| D17 | USB payload installs | Decision | A payload installed over USB always boots next |
| D18 | Web stack | Decision | Access point, captive portal, lwIP httpd, small static UI, JSON API |
| D19 | Wi-Fi security | Decision | WPA2, fixed documented default password, banner until changed |
| D20 | Wi-Fi on-time | Decision | Off after 10 minutes without a client; a setting |
| D21 | Home Wi-Fi | Decision | Not in the first release; an option for later releases |
| D22 | Features | Decision | Status, settings, OTA, diagnostics, "original IPL once", backups, CLI tool |
| D23 | Storage | Decision | Boot control and settings in two-sector records that survive power loss |
| D24 | Releases and tools | Decision | USB install UF2, update file, Python tools |
| D25 | Build and test | Decision | SDK 2.3.1, CI, PIO simulator, host tests for the loader logic |
| D26 | LED | Decision | Dark during the injection (D9), status after |
| D27 | Clock and flash speed | Decision | 250 MHz during the injection, flash divider 3 (83 MHz); both build settings |
| D28 | Phases | Decision | Hardware checks first, then phases 1–5 as listed |
| D29 | License | Decision | GPL-2.0-only, as PicoBoot-DX |
| D30 | AP security details | Decision | WPA2-AES only, default password `picoboot`, SSID from the chip ID |
| D31 | Boot-timing measurement | Decision | The Pico measures the margin itself with a passive PIO counter |
| D32 | Early readout | Decision | A minimal Wi-Fi report page in phases 0 and 1 |
| D33 | Rail check | Decision | Average current plus a stress test instead of a scope |
| D34 | Test console | Decision | DOL-001 with SD2SP2 and HDMI adapter; DOL-101 untested |
| D35 | Releases | Decision | No pre-releases; test builds 0.N.0-dev; first release 1.0.0 |
| D36 | CS glitch protection | Decision | A clock-qualified count of the 224 skipped pulses; CS left as reset leaves it (1.1.1: the pull-up removed) |
| D37 | Last flash sector | Decision | Kept free; payload 2 is 4K shorter |
| D38 | Wiring | Decision | README recommends 5 V to VSYS; 3V3 stays supported |
| D39 | Boot log | Decision | Up to the last 32 boot reports in the loader region's upper 8K |
| D40 | Settings | Decision | Their values and defaults: channel 6 (1–11), Tx power in named levels (medium), country worldwide |
| D41 | Settings changes | Decision | At once; Wi-Fi changes restart the AP and stay on trial until a client joins; the password is write-only |
| D42 | Payload uploads | Decision | Names you choose; into payload 2 while the built-in boots; the chosen payload can't be deleted; backups without the password; boot control format 2 with loader 3 |
| D43 | Ordinary `.dol` files | Decision | A loader stub in the payload (release 1.1.0): real mode, low memory as gekkoboot fills it, the sections, the cache sync; `.dol` files that reach the payload image or low memory are refused |
| D44 | CS timing diagnostics | Decision | Test builds time every CS period of the IPL read with a passive recorder, and keep a summary per boot; releases unchanged |
| D45 | Noise warning | Decision | The Diagnostics tab warns when a boot counts more CS transfers than a clean one: noise on CS (release 1.1.2) |
| D46 | Status polling | Decision | The page asks for the status one request at a time, and backs off after a failure (release 1.1.2) |
| D47 | Length-based injection | Decision | A second way to find the first IPL block, by its length instead of a count of 224; a feasibility stage first: a simulator prototype, a CLK capture test build, a length test build, then the owner decides on a release |

## 1. Goal and scope

**D1 (Decision): Pico 2 W only.** RP2350A, CYW43439, 4 MB flash. The Pico W, Pico and Pico 2 stay with PicoBoot-DX.

**D2 (Decision): the same job as PicoBoot-DX, plus over-the-air updates.**
- Inject the IPL payload at power-on, exactly as PicoBoot-DX does.
- Add a Wi-Fi access point with a web page to update the firmware and the payload.

**D3 (Decision): a new project, built from the ground up.** PicoBoot-DX code may be reused where it fits, but the design isn't bound to it.

**D4 (Decision): a drop-in for PicoBoot-DX on a Pico 2 W.**
- **Wiring:** the same, CS GP4, CLK GP5, DATA GP6. Power through 3V3, or 5 V through a diode to VSYS, as today.
- **Payloads:** the same format (`IPLBOOT` header, `PICO` footer) at the same address, `0x10080000`. Every existing PicoBoot payload UF2 keeps installing over USB.
- **Fallback:** the same built-in gekkoboot, used when there is no valid payload.
- **Upgrade from PicoBoot-DX:** one install UF2 over USB. It leaves the installed payload alone.

**D5 (Decision): non-goals.**
- other boards;
- checking for updates on the internet (needs HTTPS and a server);
- Bluetooth;
- HTTPS for the web page;
- re-injecting after the console's reset button (PicoBoot-DX doesn't either).

**D38 (Decision): recommend the 5 V wiring.**
- **Measured in phase 0:** with the 3V3 wiring, the console's 3.3 V at the Pico is 3.21 V with the radio off and 3.15 V with it on. That's below the 3.2 V where the CYW43439 guarantees full RF performance.
- **Still worked:** range (−42 dBm at 5 m) and a 10-minute full-load stress test were fine anyway.
- **README:** it recommends 5 V through a Schottky diode to VSYS. The Pico's own regulator then feeds the radio from 5 V.
- **3V3 wiring:** stays supported.

## 2. Constraints

These follow from the research and bind every decision below.

| # | Constraint | Source |
|---|---|---|
| C1 | **Injection timing:** the PIO has to count CS pulses from the console's very first IPL read. Nothing slow may run before the PIO starts, and nobody has measured how much margin there is. | [hardware.md](research/hardware.md) §3 |
| C2 | **No second injection:** a reboot while the console is on (update, watchdog, rollback) must not inject again. The PIO would count later EXI reads and drive the data line in the middle of a game. | [ota-research.md](research/ota-research.md) |
| C3 | **Payload address:** existing payload UF2s write `0x10080000` with the rp2350-arm-s family (and rp2040 blocks, which the RP2350 ignores). They must keep landing at flash offset `0x80000`. | [boot-rom.md](research/boot-rom.md) §5 |
| C4 | **Payload size:** up to 1.5 MB. Swiss r2119 is 1.29 MiB, and the console reads at most about 1.69 MiB. | [ecosystem.md](research/ecosystem.md) §2 |
| C5 | **Power:** the radio runs from the console's 3.3 V rail (or from VSYS with 5 V wiring). Tx bursts can reach 320 mA at full power. | [hardware.md](research/hardware.md) §1 |
| C6 | **Flash access:** while flash is erased or programmed, XIP reads fault. During the injection the payload DMA reads flash, so nothing may write flash then. | [boot-rom.md](research/boot-rom.md) §8 |

## 3. Flash layout

**D6 (Decision): 256K slots, two 240K Wi-Fi areas, payload 1.5 MB at `0x80000`, a second payload, all 4 MB used.**
- **Origin:** the owner's first proposal (slots A/B 256K, payload 1 MB, Wi-Fi 500K), adjusted after the research.
- **Payload:** 1.5 MB, because 1 MB is 301 KB too small for Swiss r2119 (C4).
- **Upper 2 MB:** used, since the Pico 2 W has 4 MB of flash.
- **The owner chose this over the swap with 240K slots and 256K Wi-Fi areas.**

| Region | Flash offset | Size | Holds | Written by |
|---|---|---|---|---|
| Loader | `0x000000`–`0x002000` | 8K | The loader (D7), 2.7K | USB install only |
| Boot log | `0x002000`–`0x004000` | 8K | Up to the last 32 boot reports (D39) | Firmware |
| Boot control | `0x004000`–`0x006000` | 8K | Active and pending slot, payload choice (D23) | Firmware |
| Settings | `0x006000`–`0x008000` | 8K | Wi-Fi and user settings (D23) | Firmware |
| Wi-Fi A | `0x008000`–`0x044000` | 240K | CYW43 blob for slot A (D14) | Install, OTA |
| Wi-Fi B | `0x044000`–`0x080000` | 240K | CYW43 blob for slot B (D14) | OTA |
| **Payload 1** | `0x080000`–`0x200000` | **1.5 MB** | The payload at the PicoBoot address (C3) | USB, OTA |
| Slot A | `0x200000`–`0x240000` | 256K | Firmware | Install, OTA |
| Slot B | `0x240000`–`0x280000` | 256K | Firmware | OTA |
| Payload 2 | `0x280000`–`0x3FF000` | 1.5 MB − 4K | Second payload (D15) | OTA |
| Reserved | `0x3FF000`–`0x400000` | 4K | Nothing: stock RP2350 UF2s write picotool's E10 block here (D37) | — |

**Why it fits:**
- **Slots:** the measured stand-in firmware is 165 KB, the built-in gekkoboot included, which leaves about 90 KB for the web UI and features ([sizes.md](research/sizes.md)).
- **Wi-Fi areas:** the blob is 225,240 bytes (Wi-Fi only) or 232,596 bytes (with Bluetooth), against 245,760 available.
- **Payload 1:** stays at `0x80000`, below the 2 MB line, where PicoBoot-DX had it.

**Margins and risks:**
- **Wi-Fi areas:** 13–20 KB to spare. A much larger future blob would need a layout change, which means a USB install.
- **No partition table:** the layout lives only in the loader and the firmware, never in flash. That's what keeps USB payload UF2s working (C3).

**D39 (Decision): a boot log in the loader region** (changes D6).
- **Why:** step 1.10's console tests need 60+ power-ons. Phase 0 showed that a log read over Wi-Fi makes that practical: one phone connection for a whole series.
- **Where:** the loader region shrinks to 8K, since the loader is 2.7K. The freed 8K (`0x002000`–`0x004000`), two sectors, keep a ring of 256-byte entries: up to the last 32 boot reports. When the ring wraps it erases a sector of 16, so it holds 17 to 32.
- **When it's written:** every boot, warm ones included, after the injection and before Wi-Fi starts, so nothing reads flash in the background (C6).
- **Torn entries:** a torn entry in the middle of a sector makes the ring move on to the next sector, so the newest entries are never erased (phase 0 review finding 2).
- **After release:** it stays as part of the diagnostics (D22).

**D37 (Decision): keep the last flash sector free** (changes D6).
- **Found in phase 0:** a USB install of a picotool-built RP2350 UF2 also writes picotool's RP2350-E10 workaround block (256 × `0xEF`, absolute family, address `0x10FFFF00`).
  - Without a partition table the boot ROM accepts it.
  - The address wraps on the 4 MB flash, so the block lands in its last page.
  - This holds for PicoBoot-DX, pico-examples and our own SDK builds alike.
- **Payload 2** ends at `0x3FF000`: 1,568,768 bytes, more than Swiss r2119's 1,349,956. A larger payload can only go into payload 1.
- **Our own install UF2** (D24) leaves the E10 block out. E10 only matters with a partition table, which we don't use.

## 4. Boot

**D7 (Decision): a loader of our own with `rom_chain_image()`, and no partition table.**
- **What it is:** a small image at flash offset 0 that the boot ROM starts as a normal flash image.
- **What it does:** reads boot control, picks a slot and hands over with `rom_chain_image()`. The ROM maps the chosen slot to `0x10000000` through address translation, so slot A and slot B run the same binary.
- **Why not the boot ROM's A/B:**
  - **USB payloads:** with a partition table the ROM routes every non-absolute UF2 into a partition, so payload UF2s would no longer install over USB (C3).
  - **Switching:** the ROM's A/B always erases one image when you switch.
  - **Install:** it takes three UF2 files.
  - **A2 chips:** they also have erratum E10, which breaks drag-and-drop UF2 installs once a partition table exists.
- **What it costs:**
  - one extra ROM image scan before the firmware starts (D10);
  - our own fallback logic (D12);
  - a small hook so the Wi-Fi blob loads from a fixed address (D14);
  - the loader itself can only be updated over USB.
- **Rules for the loader:**
  - tiny;
  - no clock setup;
  - no flash writes;
  - no Wi-Fi;
  - all its decisions in one pure function, tested on the host (D25).

**D8 (Decision): inject only on a real power-on.**
- **The rule:** a boot is a power-on boot only when both hold:
  - POWMAN `CHIP_RESET.HAD_POR` is set;
  - our marker in a POWMAN scratch register is missing.
- **Why both:** POWMAN scratch survives watchdog and software reboots and is cleared only at power-on, brown-out or RUN. So the pair still works if HAD_POR turns out to be sticky (to be checked, [boot-rom.md](research/boot-rom.md) §7).
- **Brown-out:** a brown-out (HAD_BOR) does **not** count as a power-on. If the Pico browns out during a Tx burst while the console keeps running, a second injection would hit a running console (C2).
- **What it controls:** only the power-on decision. Everything else that has to survive a warm reboot goes in POWMAN scratch: the session marker, the boot reason, trial state and crash counts.
- **Confirmed in phase 0:** HAD_POR stays set after watchdog and ROM reboots, since `CHIP_RESET` still read `0x00010000`. So `CHIP_RESET` alone can't tell a warm reboot from a power-on. The marker caught every warm reboot, including one during a game, and the game kept running.

**D9 (Decision): Wi-Fi starts only after the injection has finished.**
- **Why:**
  - the CYW43 start-up loads a 225 KB blob from flash over the bus the payload DMA needs;
  - the radio needs at least 150 ms after power-up;
  - its current surge would land in the middle of the console's IPL read.
- **What it costs:** the W LED (on the CYW43) stays dark during the injection (D26). PicoBoot-DX starts the chip during the injection to blink the LED; the owner chose not to.
- **Warm reboots:** without an injection, Wi-Fi starts right away.
- **"Original IPL once" (D22):** no injection either, but the console reads its own IPL then, so Wi-Fi waits 1.5 s (phase 4 review).

**D10 (Decision): a boot-time budget, measured before anything is built on it.**
- **How:** the Pico measures the margin from "PIO ready" to the console's first CS itself (D31). There is no scope.
- **What:**
  - PicoBoot-DX, as the baseline (phase 0);
  - our firmware behind the loader (phase 1).
- **Warm boards:** repeat on a warm board (upstream #148).
- **Phase 0 baseline** (PicoBoot-DX on the DOL-001): 164–168 ms from "PIO ready" to the first CS, cold and warm. `main()` to "PIO ready" takes about 0.3 ms.
- **Phase 1 result** (our firmware behind the loader, same console): 159.3–163.6 ms, cold and warm. The loader path costs **3–5 ms**; `main()` to "PIO ready" is still about 0.3 ms.
- **The rule (set in phase 1, step 1.11):** everything before "PIO ready" (loader, firmware start-up, boot control, payload choice) may add **at most 10 ms** over PicoBoot-DX, so the margin stays above about 155 ms on this console.
  - Today's cost is 3–5 ms, so later phases have about 5 ms left before the injection. Work that needs more, such as hashing a payload, happens after the injection or at upload time.
  - Every test build's report page shows the margin, so each phase's console tests check the rule.
- **If the loader is too slow:** the loader starts the PIO state machines itself before chaining, and the firmware takes them over. Not needed after phase 1.

**D31 (Decision): the Pico measures its own boot timing.**
- **The counter:** a third, passive PIO state machine watches CS. It doesn't touch the two injection programs.
  - It raises a PIO IRQ flag on the first CS falling edge, which the CPU (idle during the injection) timestamps.
  - It counts every CS pulse until the injection ends.
  - The DMA transfer count gives the number of 1 KB transfers delivered.
- **The margin:** the time of the first CS minus the time of "PIO ready".
  - A positive margin together with a successful boot proves the PIO was in time.
  - A short CS count shows missed pulses.
- **Reuse:** the same counters become the boot diagnostics (D22).
- **What it can't see:** the supply ramp before the RP2350 leaves reset. That doesn't matter, because the margin is measured from the Pico's own start.

**D36 (Decision): guard the 224 skipped pulses against CS glitches.**
- **The failure, found in phase 0:**
  - PicoBoot's occasional blue screens come from one false CS falling edge 42–44 ms after "PIO ready", when the console first drives CS.
  - The owner has also seen them with PicoBoot-DX up to v0.10 and with upstream PicoBoot.
  - `on_transfer` counts the false edge as one of the 224 skipped transfers, so the payload goes out one transfer early.
  - Rate: 2 of 56 cold boots, and both blue-screened.
- **Fix 1, a pull-up on CS (1.0.0 to 1.1.0; removed in 1.1.1):**
  - PicoBoot never sets the pad pulls, so CS keeps the RP2350's reset pull-down. That holds the undriven CS low ("selected") for the console's first ~42 ms.
  - Up to 1.1.0 the firmware turned on the CS pull-up instead. Phase 0 had 0 failures in 50 boots that way.
  - **On a second DOL-001 it failed** ([cs-pull-up.md](research/cs-pull-up.md)). With the pull-up, about half the boots got one to four extra CS transfers. They're clocked, so fix 2 counts them too, and the console booted its own IPL. Without it, that console booted every time in the tests: 14 of 14 on test builds, and 21 of 21 on 1.1.1 ([release-1.1.1-results.md](research/release-1.1.1-results.md)). PicoBoot-DX has the same pull-up from v0.11, and failed there the same way.
    - **That console's wiring was marginal:** loose wires straight to U10, which later picked up noise at its TV even without the pull-up ([main-console-tv.md](research/main-console-tv.md)). So the pull-up's harm may need marginal wiring [inf]; the dev console's flex cable never showed it. Dropping it stands either way: without it, the second console booted on those same wires.
  - **Since 1.1.1, CS is left as reset leaves it,** as PicoBoot does, and fix 2 alone guards the 224. On the dev console, 3 power-up glitches in 91 boots without the pull-up were all ignored (phase 1, and the 1.1.1 series).
- **Fix 2, a clock-qualified count:**
  - `on_transfer` counts a CS low period among the 224 only once CLK rises in it **while CS is still low** (`jmp pin` on CS). A pulse without clocks merges with the next real transfer.
  - A CLK pulse while CS is high (EXI channel 0's clock is shared with memory card slot A and SP1) counts nothing; the phase 0 review found this case.
  - The 225th transfer's first address clock must still reach `clocked_output`; phase 1 checks this in the simulator.
  - A passive counter with this rule caught the glitch on hardware, and agreed with the raw count on every good boot.
- **Diagnostics:** the D31 raw and clocked counters stay. The boot report flags a difference: a glitch the injection survived.
  - **Far more transfers than expected** (225 more than the payload's transfers: 285 for the built-in, 1544 for Swiss r2119) mean noise on CS during the transfers. On the second console, a long ground wire did that, most likely because the data line's switching bounced the Pico's ground ([cs-pull-up.md](research/cs-pull-up.md)).
  - **A few extra on most boots** came from the second console's loose wires at its TV. New, shorter wires fixed it ([main-console-tv.md](research/main-console-tv.md)).
- **Proven in phase 1:** with PicoBoot's pad setting deliberately restored (a test mode), 2 power-up glitches in 40 boots, 36–37 ms after "PIO ready". Both were ignored, and Swiss started. The 1.1.1 series repeated this: 1 glitch in 51 boots (48 of them logged), at 36.5 ms, ignored. The test mode is gone in 1.1.1, since that setting is now the normal one.
- **Known limit (phase 1 code review; the owner chose to keep the proven programs):**
  - "CLK rises" is checked as "CLK reads high" (`wait 1 gpio`), not as an edge.
  - If CLK already reads high when a false CS pulse comes, the pulse still counts. On A2 chips that could happen if the console drives CLK high at power-up and then releases it, since RP2350-E9 can then hold the pad at about 2.2 V.
  - Not seen: CLK was low in all three glitches caught.
  - **Since 1.1.1 this limit is unguarded:** without the pull-up, the false edge comes on every console that leaves CS undriven at first, and nothing else stops it if CLK reads high then.
  - The fix, if it's ever needed, is one `wait 0 gpio CLK` before each of the two waits (`on_transfer` and the passive counter).
- **Compatibility:** the injection programs now differ from PicoBoot-DX v0.10. D4's compatibility covers the wiring, the payload format and the address, not the PIO code.

**D44 (Decision): CS timing diagnostics in test builds** (2026-11-22, the owner's choices; research in [cs-timing-diagnostics.md](research/cs-timing-diagnostics.md)).
- **What it measures:** every CS-high and CS-low period during the IPL read, at 8 ns per count, and CLK's level at each falling CS edge.
  - A third passive state machine in pio1, `cs_timing` (`pio/timing.pio`, 10 instructions), next to D31's counters, writes two 32-bit words per CS low period: the CLK bit with the CS-high time before it (31 bits), then the CS-low time (32 bits). No time wraps around.
  - A DMA channel copies the words into a 32 KB buffer in `.uninitialized_data` (4096 periods). The test builds' boot-log rows, which grow with the summary, are kept there too, so the test builds zero less before the injection than a release.
  - Each time between two edges reads 8–20 ns short; a pulse within about 16 ns of an edge merges into it.
- **Passive:** it only reads CS and CLK. It starts with the counters before "PIO ready" and stops with them after their snapshot. pio0 and the injection are untouched.
- **Test builds only** (`-dev`, like the test page). A release build of the same source is byte for byte the release.
- **The summary,** computed after the injection and before Wi-Fi; "nothing measured" on boots without an injection:
  - the shortest and the longest CS-high gap in BS1 (transfers 1–224), and in the payload;
  - the number of CS-high gaps under 100 ns, and the first one's transfer number (500 ns until T3 measured the real gaps: 136 ns or more);
  - the CS-low times of transfers 225 and 226;
  - the number of falling CS edges with CLK high.
- **Where it shows:**
  - the test page;
  - `/api/diagnostics` in test builds;
  - the raw record of the current boot, as a download (`/api/test/cs-timing`). A first period with no CS-high time means CS was low when the recorder started (the dev console's undriven start), so the record describes itself. Any restart of the Pico loses it.
- **The boot log in test builds:** the report gains the summary, under the log's own version for test builds, so test builds and releases don't read each other's entries. After a switch, the first boot erases one of the log's two sectors and the 17th the other: switching back sooner brings the older entries back.
- **What it decided (T5, the owner, 2026-11-22),** from 33 power-ons on both consoles ([cs-timing-results.md](research/cs-timing-results.md)):
  - the read's timing is the same on both consoles: BS1 gaps of about 150 ns, 192 ns at the hand-over to the first block, about 330 ns between blocks, a ~26.8 MHz EXI clock [inf];
  - **next:** a wiring-quality number for the releases, the shortest CS-high gap of each boot;
  - **not now:** a minimum CS-high gap for D36's count, which would have only about 100 ns of room; the fourth wire stays a later-releases item;
  - **the code stays on its branch,** `diag-cs-timing`, not merged into `main`; a later test build can take it from there. Release builds don't have it either way.

**D45 (Decision): a noise warning from the CS counts** (release 1.1.2, the owner's choices; research in [wiring-quality.md](research/wiring-quality.md)).
- **The rule:** a clean boot counts 224 or 225 plus the payload's transfers (`payload_words / 256`), as every clean boot recorded has. A boot whose clocked count is higher had noise on CS during the read: a blip splits a transfer, and an extra one during BS1 sends the payload early. D36 can't tell a clocked extra from a real transfer.
- **Where:** the Diagnostics tab.
  - "This boot" gives the number of extra transfers, and points to the README's wiring advice.
  - The boot log table marks such boots.
  - A glitch (raw above clocked) keeps its own note.
- **The page only:** the rule is in the page's own script, from fields the API already has (`payload_words`, `cs_raw`, `cs_clocked`, `result`). No firmware logic, API or boot-log change, and older log entries get it too.
- **Checked by hand,** like the page's other warnings. Neither console has noisy wiring now, so the page is shown made-up diagnostics with the recorded counts.
- **For boots with an injection only** (complete or stalled): the others count nothing.
- **A known exception:** boot 108 on the second console's fixed wiring ([main-console-tv.md](research/main-console-tv.md) §4): the built-in, complete, 5368 transfers, most likely the console restarting its reads. The rule marks it as noise. A boot that stalls counts for the whole 10 s time-out, so a console that reads again in that window would look the same [inf].
- **Not the shortest CS-high gap** (D44) in the releases: every blip that shortens a gap is an extra transfer, which the counts already show.

**D46 (Decision): the page's status polling on a weak link** (release 1.1.2, the owner's choice; research in [weak-link.md](research/weak-link.md)).
- **The polling:** the Status and Payloads tabs ask for `/api/status` one request at a time.
  - After a request fails or times out, the next comes after 10 s, then 20 s, then every 30 s.
  - After a success, every 5 s again. A success from any request brings a longer wait back to 5 s.
  - **After an action** (a boot choice, Keep, a payload change or upload, a settings save or restore, the wait after a restart), the page sends a request of its own instead of sharing one on its way, so it shows the state after the action. Each request has a number, and an answer older than the one shown is dropped.
  - The polling carries on after an error; a 200 answer without a body counts as a failure.
- **Why:** on a weak link, overlapping and abandoned requests leave connections on the Pico that lwIP keeps resending for minutes. They fill its 8 TCP connections and 32 send segments, so new requests can't get through until lwIP drops them.
  - lwIP drops a connection only after 12 retransmissions, each time-out doubling (`TCP_MAXRTX`).
  - The web server closes a stalled connection gracefully, after about 8 s.
  - All its connections share one priority, so a new one can't push out a stuck one.
- **The page only:** the schedule is a one-line function next to the poller, in the page's own script.
- **Not changed for now:** lwIP's `TCP_MAXRTX`, the web server's close, the number of connections.

**D47 (Decision): length-based injection, a feasibility stage first** (2026-11-23, the owner's choices; research in [length-injection.md](research/length-injection.md) and [gc-docs.md](research/gc-docs.md)).
- **The method,** after the Shuriken attack: the first transfer still running after 96 clocks is the first IPL block. BS1's fetches are 96 clocks, a 32-bit command and 8 bytes; a block is 8224.
  - **The Pico takes the data line over inside the first block's 32-byte header,** which BS1 doesn't use. Later blocks start after the command, as now.
  - **No pin lifted and no payload change:** PicoBoot already drives the data line against U10. The DMA stays as it is, and the output program drops the header bits U10 has sent.
  - **What it ignores, unlike D36's count:** pulses without clocks, whatever CLK's level (D36's known limit), and noise that splits BS1 transfers.
  - **What it takes that the count ignores** (L7): one CLK glitch while CS is low during BS1 makes a fetch's 97th edge, so the payload starts in BS1. Today's count takes one clock per transfer and ignores the rest. On clean wiring no CLK phase was under 16 ns (L4); on noisy wiring, CLK may be noisy too. Weighed at L9: the method is an opt-in with the count the default, so the risk applies only to consoles switched to it.
- **The programs** (`src/firmware/pio/length.pio`, plan step L1, tried in the PIO simulator):
  - **The detector,** in pio1 next to D31's counters, 14 instructions (pio1 then holds 24 of its 32). It polls CS (`JMP_PIN`) and CLK (`IN_BASE`, `IN_COUNT` 1) at every clock. After the fall of the 97th clock with CS still low, it raises IRQ 0 in pio0 (`irq prev`), and then one at every CS fall, as `on_transfer` does after its count.
  - **The output program,** in pio0, 26 instructions: `clocked_output` with an entry for the first block. It drops the first block's data bits 0–65, which U10 sends, puts bit 66 on the pin while it's still an input, and drives it one instruction after clock 97, the timing of every other bit.
  - **The switch point (L1): bit 66.** In the simulator, both methods work from a 32 ns clock up and fail at 24 ns, the output loop's own limit; BS1's clock is 37.04 ns (L4).
  - **Each later block waits for CS high before its IRQ** (L1's finding, the owner's choice). A CS blip that splits a payload block leaves IRQ 0 set early. Without the wait, the output program takes that block's last clock as the next block's first command clock, and every later block arrives one bit late. No clock runs between blocks, so after the wait it stays in step.
- **Today's injection has that weakness too** (L1, in the simulator): `clocked_output` has no CS wait. The owner's choice: the wait goes into today's `clocked_output` in a release of its own, with its own design questions (the plan's later releases).
- **The name:** "Injection: standard / length-based". Standard is D36's count, and stays the default.
- **The feasibility stage** (the owner's choices):
  1. **A simulator prototype:** the detector and the output program in `tests/pio_sim.py`, with the cases of the research's §6. Tests only.
  2. **A CLK capture test build,** from `diag-cs-timing`. The detector polls CLK every 12 ns and needs its high and low phases longer than that; nobody has measured them.
     - A passive state machine samples CS and CLK every 4 ns, the detector's own rate, from the first rising CLK edge inside a CS low period, for about 1 ms: BS1 and the start of the first block, into a 64 KB buffer. A CLK glitch while CS is high doesn't start it (L3).
     - The summary gives CLK's shortest and longest high and low phase, and BS1's phases counted by length, 1–7 and 8 or more samples, so one glitch shows as one (L3). The raw capture downloads per boot, like D44's record.
     - Test builds only. The injection is D36's count, so every boot behaves as now.
     - The branch has release 1.1.2 merged in, so the test build has its page; its CI also compiles a test build (L3).
  3. **A length test build,** on its own branch `length-injection`: the length method only, with no switch, and the detector adjusted to what the capture showed. If a boot fails, the console starts its own menu, or shows a black screen when a misaligned payload still reports complete (L7). The owner then boots the other slot from the Firmware tab, as Wi-Fi still comes up.
  4. **Console series on both consoles,** on clean wiring only, about as T3's: about 10 cold boots per console with the built-in and Swiss, and a few quick power cycles on one.
     - **As run (L8): the dev console only,** the owner's choice, since L4 measured the same timing on both. All 13 power-ons started their payload, quick power cycles included.
  5. **The owner decides** on a release from the results.
     - **The decision (L9): one release, 1.2.0.** Today's injection gets the CS wait (L1's finding), and the length method becomes an opt-in, with D36's count the default ([length-injection-results.md](research/length-injection-results.md) §7).
- **The release's switch** starts from the research's §5 and §7: the Payloads tab, a persistent choice, a flag bit in boot control, and a byte in the boot report's padding. 1.2.0 has its own design questions, design and plan.
- **The code stays on its branches,** `diag-cs-timing` and `length-injection`, not merged into `main` during the stage.

**Boot flow:**

```
power on -> boot ROM -> loader (flash offset 0)
  |- power-on?  HAD_POR set and no session marker: set marker, power_on = 1
  |- pick slot: pending trial slot, else the active slot (D12),
  |             else the other slot if this one keeps crashing
  |- boot info -> POWMAN scratch (power_on, slot, trial, reason)
  `- rom_chain_image(slot) -> firmware (slot A or B, mapped to 0x10000000)
       |- if power_on: 250 MHz; PIO + DMA inject payload 1, 2 or built-in;
       |               no flash writes; wait for disable_chip; lower the clock
       |- start the watchdog, then Wi-Fi (blob from Wi-Fi A or B)
       |- AP, DHCP, DNS, web UI; Wi-Fi auto-off (D20)
       `- trial bookkeeping (D12), USB payload check (D17)
```

## 5. Firmware updates

**D11 (Decision): one update file per release, checked with SHA-256, not signed.**
- **The file:** `picoboot-wifi-X.Y.Z-update.bin`, made in CI. It holds:
  - a header: magic, format version, firmware version, board, sizes, SHA-256 of each part;
  - the slot image;
  - optionally the Wi-Fi blob.
- **On the device:**
  - it streams the image into the inactive slot while hashing it, then reads it back and hashes again;
  - only then does it mark the slot as pending and reboot.
- **USB-only images:** a file that's meant only for USB (the install UF2) is refused.
- **No signing:** the Wi-Fi password is the gate (D19). The owner's own test builds install without extra steps. Signing (a private key in CI, about 10 KB of code) can come in a later release.
- **The format** is in [update-format.md](update-format.md) (phase 3). The owner chose:
  - the Wi-Fi blob always in the release file, written only when the paired area holds another one;
  - downgrades allowed, with a warning in the UI.
- **Compatibility:** the header names the flash layout and the lowest loader version the firmware needs. The loader changes only with a USB install (D24), so the Pico refuses an update its layout or loader doesn't fit, and says a USB install is needed.

**D12 (Decision): a new firmware is on trial until it has proven itself.**
- **Trial:** after an update the new slot is pending, and the old slot is still the fallback.
- **Crash:** if the new firmware crashes or hangs (watchdog) while on trial, the loader boots the old slot. The old firmware records "trial failed" in boot control and shows it on the status page.
- **Keep:** the new firmware becomes the active one after one of these:
  - its first complete power-on injection, followed by Wi-Fi coming up;
  - the user pressing *Keep* on the firmware page.
- **A crash during the trial's first power-on boot:** that one console start goes to the original IPL. The firmware can't fall back and still inject in time (C1, C2). The next power-on uses the old firmware.
- **Crash loop:** a crash loop in the active firmware (N crashes in one power-on session) makes the loader try the other slot, if it holds a firmware that was kept before.
- **As built in phase 3** (step 3.4), with the owner's choices:
  - **N is 3** crashes in a row, and the fallback is **for good**: the firmware it falls back to becomes the active one, and the crashing slot is marked failed (D13 can switch back by hand).
  - **The loader remembers the session in POWMAN scratch,** since it never writes flash: the slot it started last, whether on trial, its crashes in a row, and which trials failed. Scratch survives warm reboots and is cleared at power-on, so every console session starts afresh.
  - **A crash is any reset the firmware didn't mean.** Before each reboot it means (the UI, an update), the firmware writes a clean-reboot word into scratch. A watchdog reset (8 s, fed in the injection's waits and the main loop) or a fault (the fault handler reboots at once) lacks it.
  - **The firmware records the news:** a failed trial (the slot becomes failed, nothing is pending), or the takeover after a crash loop. On keeping, the slot it replaces becomes kept as well, as a firmware that ran, so a later crash loop can fall back to it.
  - **Never a fallback:** a slot that's empty (perhaps half written), failed, or on trial isn't started in place of a slot without a valid image.
  - **No update while on trial:** the running firmware refuses one (`on_trial`), since the other slot holds the firmware to fall back to.
  - **The loader reports its version (2)** in boot info; the phase 3 firmware needs it, so it comes with one USB install (D11).
  - **After the phase 3 review:**
    - **Keeping is checked:** after a complete power-on injection, the firmware asks the Wi-Fi chip whether the access point is up (the owner's choice). A firmware on trial whose Wi-Fi doesn't come up reboots as a crash, so the loader goes back.
    - **Updates only from the active firmware,** into the other slot: never into the active slot, which would skip the trial. A firmware started in place of an active slot without an image becomes the active one.
    - **An empty slot is never started.** After a USB install, slot B counts as empty: the install doesn't touch it, so it may hold anything.
    - **An update writes the image's first sector last,** once the image's hash is right: that sector holds the block the boot ROM looks for, so a cut-off or damaged upload never leaves a slot the ROM would start.

**D13 (Decision): switch freely between two good slots.**
- The firmware page shows both slots: version, build, state (active / pending / failed / empty).
- *Boot slot X* writes boot control and reboots. Nothing is erased.
- **As built in phase 3:** a kept slot becomes the active one at once; any other slot with a firmware (from a USB install, unkept or failed) goes on trial first (D12). Going back to the active slot from a trial abandons the trial.

**D14 (Decision): the Wi-Fi blob outside the slots, one copy per slot, at fixed addresses.**
- **Pairing:** slot A uses Wi-Fi A and slot B uses Wi-Fi B. An update can then bring a new blob without touching the one the fallback firmware needs.
- **Hash:** each update file names the SHA-256 of the blob it needs.
  - If the paired Wi-Fi area holds a different blob, the device takes it from the update file, which always carries it (D11). Phase 3 dropped copying it from the other area.
  - Normally the blob doesn't change, and an update is only the ~170–200 KB slot image.
- **Loading:** the CYW43 driver reads the blob through the untranslated XIP window (`0x1c000000` + offset). This replaces the SDK's partition-table lookup, a hook of a few lines ([sizes.md](research/sizes.md)).
- **Which blob:** Wi-Fi only (225 KB), since Bluetooth is a non-goal.

## 6. Payloads

**D15 (Decision): two payload slots; the user chooses which one boots.**
- **Two complete payloads:** payload 1 (the USB address) and payload 2 each hold a full payload, e.g. Swiss and gekkoboot side by side.
- **Uploads:** an upload goes to the slot that isn't booting and switches to it only after it has been verified. The previous payload stays as a choice.
- **Power cuts:** no copying, so a power cut during an upload never touches the payload that boots.
- **Fallback:** the built-in gekkoboot is always a third choice, and boots when the chosen payload is invalid.

**D16 (Decision): what the device accepts, and where `.dol` files are converted.**
- **Payload UF2:** any family. The device takes one family's blocks at `0x10080000` onwards, so gekkoboot's RP2040-only UF2s work too, which they don't over USB on a Pico 2.
- **Raw payload:** the `IPLBOOT` … `PICO` image as it sits in flash.
- **Checks:** every upload is checked for header, footer, size (≤ 1.5 MB for payload 1; ≤ 1.5 MB − 4K for payload 2, D37) and 1 KB alignment before it can be chosen.
- **`.dol` files:** converted to a payload in the browser (JavaScript port of `tools/process_ipl.py`) and in the CLI tool (the Python original). The device doesn't convert them: DOL sections need random access, which a streamed upload doesn't give.
  - A `.dol` built as an IPL replacement (loaded at and started from `0x81300000`) becomes the payload image itself.
  - A `.dol` loaded elsewhere gets D43's loader stub in front. One loaded at `0x81300000` with another entry point is refused, as before.

**D43 (Decision): a loader stub for ordinary `.dol` files** (release 1.1.0, the owner's choices; research in [dol-loader.md](research/dol-loader.md)).
- **What it is:** a `.dol` that isn't built for `0x81300000` (an ordinary one, linked at `0x80003100` by default) becomes a payload image of a small loader stub, a table and the `.dol`'s sections. An IPL build still converts as before (D16).
- **The stub,** linked at `0x01300000` and entered at `0x81300000` (where BS1 jumps), in this order:
  1. **Start like gekkoboot's packer** (the code at the same address in our built-in): real mode first (`mtsrr1 0`, `rfi`), so nothing depends on BS1's undocumented BATs. Then it quiesces the hardware: stops a DI transfer, resets and halts the DSP, turns audio DMA off and waits for the DMAs. It clears DABR and IABR, as Swiss's newer packer does.
  2. **Low memory as gekkoboot's `__SYS_PreInit`:** zero `0x80000000`–`0x800030FF`, then the memory size (`0x28`, from the memory controller as BS1 left it), the console type (`0x2C`), the bus and CPU clocks (`0xF8`, `0xFC`) and the boot time (`0x800030D8`). The video mode (`0xCC`) stays 0: libogc sets it itself from the console's SRAM settings. The exception vectors point at a loop, so an exception before the program sets its own stops there; the system call's (`0xC00`) returns (`rfi`), as Swiss's packer leaves it and as a program gekkoboot starts finds it, because libogc's cache flushes end in `sc`.
  3. **The program:** the BSS zeroed, then each section copied to its address, in the header's order, so a section inside the BSS's range keeps its data, as in Swiss's loader; everything written back (`dcbst`).
  4. **The start:** `sync`, the instruction cache invalidated, `isync`, then a jump to the entry. No argv (argc 0).
- **Why:** booted straight from the payload, nobody fills low memory or syncs the caches, and after a long power-off memory holds garbage. cubiboot's own PicoBoot stub does neither, and fails on the owner's cold boots.
- **What the converter refuses:** a `.dol` with a section or BSS that reaches the payload image (from `0x81300000`) or low memory (below `0x80003100`), or lies outside the 24 MB of main memory. None of the `.dol` files surveyed does; the stub stays simple.
- **Built with** Ubuntu's PowerPC binutils (`binutils-powerpc-linux-gnu`) in the build image, not devkitPPC: the stub is a few hundred bytes of assembly, with no C or libraries.
- **Where it lives:** the assembly source in the repo; its bytes in the page's converter and the CLI, which each put the payload together. CI assembles the source and checks the bytes match.
- **Only the converters change:** the payload format, the firmware's C code, the loader and the injection stay as they are.
- **Correction of earlier text:** Swiss's PicoBoot build doesn't copy `.dol` sections (its xz packer unpacks one flat image); the closest existing design is cubiboot's `entry.dol`.

**D42 (Decision): payload uploads, names and backups** (phase 4, the owner's choices; details D15–D17, D22).
- **Names:** each payload has a short name you choose, up to 24 printable characters. The payload page pre-fills it from the file name (`swiss_r2119.dol` → `Swiss r2119`); it can be changed at the upload and renamed later.
  - **Why it costs a USB install:** the names live in boot control, which the loader reads. Boot control gets format 2, and the loader version 3.
  - **From then on:** the loader, and the firmware, read only the fields they know of any newer boot control record. Later formats only add fields at the end, so they need no new loader.
- **Where an upload goes:** into the payload slot that isn't the choice (D15); while the built-in gekkoboot is the choice, into payload 2, so that payload 1 keeps what a USB install put there (D17).
- **Delete:** only a payload that isn't the choice; choose another one first.
- **Backups:** plain downloads. The settings backup leaves the Wi-Fi password out, as the API never returns it (D41).

**D17 (Decision): a payload installed over USB always boots next.**
- **How it's detected:** boot control keeps a short fingerprint of payload 1: a CRC-32 of its header (with the size), of 64 words spread over it, and of its last 256 bytes (with the footer). It's quick enough to compare before the injection (C1). Builds of a payload share much of their start, so the samples tell another build of the same size apart (phase 4 review); a payload written again unchanged can't be told apart.
- **Blank boot control** (after a USB install of the firmware): payload 1 is the choice anyway; its fingerprint is recorded without a USB-install notice.
- **A delete or an upload** zeroes the payload's first page before boot control records it, so a power cut in the erase leaves no old header that would look like a USB install.
- **What happens:**
  - if the fingerprint differs, payload 1 was written over USB and is booted, whatever was chosen before;
  - after a complete injection, the firmware records the new fingerprint. Until then every power-on counts as the first one after the USB install, so the page says so at the console's power-on, not only at the Pico's own reboot on USB power after the drag-and-drop;
  - a choice, a rename or an upload over Wi-Fi records it too, so the user's choice holds;
  - the web page shows that it switched.

## 7. Wi-Fi and web

**D18 (Decision): access point, captive portal, lwIP httpd, small static UI, JSON API.**
- **Access point:**
  - `cyw43_arch_lwip_poll`, single core, everything in one main loop after the injection;
  - DHCP and DNS servers from pico-examples;
  - the DNS answers every name with `192.168.4.1`, so phones open the page by themselves.
- **Web server:** lwIP httpd.
  - The static UI is plain HTML, CSS and JavaScript, gzipped, under about 30 KB, built into the firmware.
  - Downloads (backups) stream straight from flash through httpd's custom files.
- **Uploads:** `fetch()` with `application/octet-stream`. lwIP httpd doesn't parse multipart, and `LWIP_HTTPD_POST_MANUAL_WND` holds the upload back while a sector is erased.
- **JSON API:**
  - `GET /api/status`;
  - `POST /api/firmware`;
  - `POST /api/payload`;
  - `POST /api/settings`;
  - `POST /api/boot` (switch slot or payload, "original IPL once");
  - `POST /api/reboot`;
  - `GET /api/backup/…`.

  The CLI tool uses the same API.

**D19 (Decision): WPA2 with a fixed, documented default password.**
- **Out of the box:** SSID `PicoBoot-XXXX`, where XXXX comes from the chip's unique ID, with a default password that's in the README.
- **Change prompt:** a banner on every page until the password is changed.
- **The Wi-Fi password is the only gate:** no separate web login and no image signing (D11).
- **Forgotten password:** reinstalling the USB install UF2 resets the settings.
- **What the gate means** (phase 5 security review):
  - Anyone with the password can install any firmware. A hostile one could do what any firmware can, including writing the RP2350's one-time-programmable memory, which a USB reinstall can't undo. Signing (D11) is what would close that.
  - Such a client can also wear out the flash by changing settings or choices over and over. A store that changes nothing writes nothing, but the rest is accepted: it needs the password, which already allows worse.
  - A web page elsewhere can read the GET answers through DNS rebinding, though it can't change anything (POSTs need `Host: 192.168.4.1`). So no GET answer holds the password or another secret (the owner's choice).
- **Details:** see D30.

**D20 (Decision): Wi-Fi turns off after 10 minutes without a client.**
- **When it runs:** the AP starts after every boot (after the injection, D9) and stays on while a client is connected.
- **Setting:** 2, 5, 10 or 30 minutes, or always on.
- **Why:**
  - it saves about 40 mA plus Tx bursts on the console's rail (C5);
  - it limits radio noise during play;
  - it limits exposure.
- **To reach it again:** power-cycle the console.
- **Timing:** the countdown starts when the Wi-Fi chip drops the last client. A device that leaves without saying goodbye stays on the chip's list until it times out, so Wi-Fi can go off about 1.5 minutes later than set (phase 2 bench check: 3.5 minutes for the 2-minute setting). The owner accepted this.

**D21 (Decision): no home Wi-Fi in the first release.**
- **What it would be:** station mode with mDNS (`picoboot.local`), falling back to the AP when the network can't be joined.
- **Status:** kept as an option for later releases. It costs about 10 KB and a settings page, which fits in the slots' margin (D6).

**D30 (Decision): access point security details.**
- **WPA2-AES only.** WPA3/WPA2 transition can come later, once phase 2 shows that phones and laptops connect reliably.
- **Default password:** `picoboot` (8 characters, the WPA2 minimum), documented in the README.
- **SSID:** `PicoBoot-` plus the last four hex digits of the chip's unique ID, so two boards in one room can be told apart.

## 8. Features and storage

**D22 (Decision): the feature set of the first release.**

| Feature | Phase |
|---|---|
| **Status:** firmware version and build, active slot, the other slot's version and state, payload slots (name, size, SHA-256), uptime, temperature, Wi-Fi clients | 2 |
| **Diagnostics of the last boot,** and a log of the last 32 (D39): power-on or warm boot and why, loader decision, payload used and why, CS pulses seen, transfers delivered, injection complete or stalled (and after how many transfers), time to "PIO ready", time to Wi-Fi up | 2 |
| **Settings:** SSID, password, channel, country, Tx power, Wi-Fi auto-off, LED on/off | 2 |
| **Firmware:** upload, progress, verify, trial/keep, switch slots | 3 |
| **CLI tool:** status and firmware update first, then payload upload and `.dol` conversion | 3–4 |
| **Payloads:** upload, choose, delete, download | 4 |
| **"Boot the original IPL once":** skip the injection at the next power-on, for changing the console's clock or language | 4 |
| **Backup:** download payloads, the firmware slot and settings | 4 |

Home Wi-Fi (D21) is left out of the first release.

**D23 (Decision): boot control and settings survive power loss.**
- **Layout:** each area has two 4K sectors. Every write goes to the other sector, as a record with a sequence number and a CRC. The newest valid record wins, so a power cut during a write leaves the previous record.
- **When writes happen:** only after the injection, never in the loader (C6). Writes are rare (an update, a switch, a settings change), so wear doesn't matter.
- **Empty or invalid:** the loader treats empty or invalid boot control as "slot A active, payload 1", which is the state right after a USB install.

**D40 (Decision): the settings, their values and defaults** (details D22's settings).
- **Stored:** a record of 128 bytes in the settings area (D23), with spare bytes for later settings in the same format version. A USB install blanks the area, which means the defaults (D19).

| Setting | Values | Default |
|---|---|---|
| SSID | 1–32 printable ASCII characters | `PicoBoot-XXXX` (D30) |
| Password | 8–63 printable ASCII characters (a WPA2 passphrase) | `picoboot` (D30) |
| Country | worldwide, or one of the 51 countries the CYW43 driver lists | worldwide |
| Channel | 1–11 | 6 |
| Tx power | low 5 dBm, medium 10 dBm, high 15 dBm, max (the chip's own limit); the owner's choice in step 2.6 | medium |
| Wi-Fi off after | 2, 5, 10 or 30 minutes, or never (D20) | 10 minutes |
| LED | on, off (D26) | on |

- **Channel 6:** the owner's choice. The driver's default, 3, overlaps both 1 and 6; channels 1, 6 and 11 don't overlap, and every country allows them.
- **Only 1–11:** the Americas don't allow 12 and 13, and a channel the country doesn't allow could keep the AP from starting.
- **Named Tx power levels:** the owner's choice, easier to pick on a phone than a dBm number. The default is below the maximum, since phase 0 found plenty of range.
- **A stored value that isn't allowed** falls back on its own default, and the other settings stay. The firmware checks every value before it stores one, so this only guards against bugs.

**D41 (Decision): how a settings change takes effect.**
- **At once, without a reboot.**
  - Tx power, LED and auto-off apply straight away.
  - A change to the SSID, password, channel or country restarts only the access point, about a second after the answer: the firmware keeps running, and the console is untouched. Phones drop off and join again.
- **Wi-Fi changes are on trial** until a client reaches the web server through the restarted access point. Phones' captive-portal probes do that by themselves when they join.
  - Then the change is kept.
  - If the firmware starts again before that, the previous Wi-Fi settings return, and the status says so.
  - **Why:** the Pico sits inside the console. Without the trial, a mistyped password means opening the console for a USB install (D19). With it, power-cycling the console is enough.
- **The password is write-only:** the API says only whether it is still the default (for the D19 banner).

## 9. Releases, install and tools

**D24 (Decision): what a release contains.**
- **`picoboot-wifi-X.Y.Z.uf2`:** the USB install, for a first install, recovery, or an upgrade from PicoBoot-DX.
  - It writes the loader, blank boot control and settings, Wi-Fi A and slot A, all at absolute addresses (rp2350-arm-s family, no partition table).
  - It doesn't touch the payloads.
- **`picoboot-wifi-X.Y.Z-update.bin`:** the OTA update (D11).
- **Tools** (Python standard library, as in PicoBoot-DX):
  - `.dol` → payload (raw and UF2);
  - update-file builder (CI);
  - CLI client for the JSON API.
- **Version:** semver, in the update header and in picotool binary info. Test builds are 0.N.0-dev (N = the phase) plus the git hash; a release is its plain version, and the first is 1.0.0 (D35).
- **How it's made** (phase 5): a tag `vX.Y.Z` that matches the version in `CMakeLists.txt` starts `.github/workflows/release.yml`. It builds and tests as CI does, and makes a draft release with the USB install, the update file, the CLI (which holds the `.dol` converter) and `SHA256SUMS`, its notes from `docs/releases/X.Y.Z.md`. The owner checks the draft and publishes it.

**D29 (Decision): license GPL-2.0-only.** PicoBoot-DX and the gekkoboot code it bundles are GPL-2.0, and this project reuses both.

**D35 (Decision): no pre-releases.** Test builds come from CI artifacts on the pushed phase branch, or from a local Docker build with the same SDK and toolchain as CI. The first GitHub release is 1.0.0.

## 10. Build and test

**D25 (Decision): build and test setup.**
- **Toolchain:** Pico SDK 2.3.1, picotool 2.3.1, CMake. The targets are the loader, the firmware, the Wi-Fi blob image, the install UF2 and the update file.
- **CI** (GitHub Actions, ubuntu-26.04):
  - builds;
  - Python tests for the tools;
  - the PIO simulator from PicoBoot-DX against the real PIO programs;
  - a layout check of each ELF against the flash map;
  - host-compiled C tests for the loader's slot decision and the boot-control records;
  - D43's stub: assembled with Ubuntu's PowerPC binutils and checked against the converters' copy, then run by a small PowerPC interpreter in Python on made-up `.dol` files, which checks the copies, the BSS, low memory and the cache sync before the jump.
- **Hardware tests:** the owner, on a real GameCube.

**D26 (Decision): LED.**
- **During the injection:** dark (D9).
- **After it:**
  - three slow blinks if it completed (as in PicoBoot-DX);
  - fast blinks if it stalled, for as long as Wi-Fi is on (the owner's choice in phase 2; PicoBoot-DX also keeps blinking after a stall);
  - on while the AP is up, off when Wi-Fi is off.
- **Setting:** LED off.

**D27 (Decision): the clock during the injection is fixed after phase 1.**
- **Start:** 250 MHz, as in PicoBoot-DX, as a build setting.
- **Upstream reports:**
  - some Picos take the overclock badly (#146);
  - a Pico 2 W batch fails when warm, blamed on slow flash (#148).
- **Phase 1 measures:**
  - the DMA's margin against the console's EXI data rate;
  - warm-board behaviour.
- **Then:** the default clock and the flash clock divider during the payload DMA are set from those numbers.
- **Set in phase 1 (step 1.11):** **250 MHz** during the injection and **flash divider 3**, both build settings.
  - The firmware sets up quad I/O flash reads itself (embedded `boot2_w25q080`); after the loader's chain, the boot ROM leaves the slowest serial mode (0.06 MB/s).
  - Divider 3 gives an 83 MHz flash clock at 250 MHz, below the W25Q32RV's 133 MHz.
  - The payload DMA through the uncached window reads 18 MB/s at 125 MHz, about 5.5 times the console's ~3.3 MB/s, and roughly double that at 250 MHz.
  - 87 console boots in phase 1, cold and warm, had no stall or fault.
  - If a board takes 250 MHz or warm flash badly (upstream #146, #148), a lower clock or a slower divider is the first thing to try.

## 11. Phases

**D28 (Decision): hardware checks first, then five phases.**
- **Phase 0 first,** before the core code. Its results can still change this design.
- **Phases 1–5** as listed below.

| Phase | Content | Done when |
|---|---|---|
| 0. Hardware checks | A measurement build of PicoBoot-DX with the D31 counter and the D32 report page, inside the closed console; the D33 rail check | Radio works on 3V3; current and rail sag measured; range known; boot-timing margin measured |
| 1. Core | Loader, layout, firmware with the injection, the power-on rule, boot info, the D31 counter, the Wi-Fi blob hook and report page (D32), install UF2, CI | The console boots a payload through the loader on a cold and a warm board; a warm reboot doesn't inject; the boot-time budget is met (D10); the clock is set (D27) |
| 2. Wi-Fi | AP, captive portal, status and diagnostics, settings, auto-off, LED | A phone reaches the status page from a closed console |
| 3. Firmware OTA | Update file, slot writes, trial and fallback, switching, Wi-Fi blob pairing, CLI tool (status, update) | An update, a failed trial and a switch all work in the console without USB |
| 4. Payload OTA | Payload slots, upload formats, browser `.dol` conversion, USB rule, "original IPL once", backups, CLI payload commands | Swiss and gekkoboot uploaded and chosen over Wi-Fi |
| 5. Release 1.0 | README and install guide, whatever phases 0–4 show is needed | Owner's hardware test passes on a fresh install and on an upgrade from PicoBoot-DX |

Firmware OTA comes before payload OTA: once it works, every later test runs without opening the console.

**D32 (Decision): a minimal Wi-Fi readout comes early.**
- **Why:** the measurements have to come out of a closed console.
- **Phase 0:** the PicoBoot-DX measurement build starts an access point after the injection. It has a plain-text report page and two load-test URLs: a large download (Tx load) and an upload sink (Rx load).
- **Phase 1:** the Wi-Fi blob hook (D14) and the same page move forward from phase 2, since the blob doesn't fit inside a 256K slot.

**D33 (Decision): the rail check without a scope.**
- **Average current:** measured with the meter with the AP off, idle, with a client connected and during the large download.
- **Rail sag:** checked with a stress test. The large download runs at full Tx power while Swiss loads a game from the SD2SP2, with the HDMI adapter on. Console glitches, video artefacts, Pico brown-outs (`HAD_BOR`) and Wi-Fi drops are recorded.

**D34 (Decision): the DOL-001 is the test console.**
- **Setup:** all hardware tests run on the owner's DOL-001, with the SD2SP2 and the HDMI adapter.
- **DOL-101:** the README says it's expected to work (same IPL path as PicoBoot) but is untested.

## 12. Risks and hardware checks

| # | Risk | Check | If it goes wrong |
|---|---|---|---|
| R1 | The loader makes the boot too slow | Phase 0: 164–168 ms margin on the DOL-001; phase 1 measures the loader | The loader starts the PIO itself, or slot A boots without a loader on the power-on path |
| R2 | Tx bursts pull the console's 3.3 V rail down | Phase 0: 3.15 V average with the radio on; stress test clean | 5 V wiring recommended (D38); lower Tx power by default |
| R3 | No usable range from a closed console | **Closed** in phase 0: −42 dBm at 5 m | — |
| R4 | Warm boards or slow flash break the injection | Phase 0: warm boots fine (the board only reached ~22 °C on its sensor); D27, phase 1 | Lower clocks or a slower flash divider during the injection |
| R5 | Wi-Fi disturbs the EXI bus or the video | Phase 0 stress test (SD2SP2, HDMI adapter): nothing seen; phase 2 again | Wi-Fi auto-off by default (D20); lower Tx power |
| R6 | HAD_POR or scratch registers behave differently from the datasheet | **Closed** in phase 0: HAD_POR survives warm reboots; the D8 marker covers it | — |
| R7 | A future CYW43 blob outgrows 240K | Each SDK update | Layout change through a USB install |
| R8 | A CS glitch at power-up shifts the injection | D36; phase 1 in the simulator and on hardware | — |
