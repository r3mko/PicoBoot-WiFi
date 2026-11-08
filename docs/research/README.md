# Research

The research behind [the design](../architecture.md), as the design cycle asks: what we already run, what exists outside, and whether to build or reuse.

| File | Date | What it covers |
|---|---|---|
| [ota-research.md](ota-research.md) | 2026-11-07 | First look at Wi-Fi OTA on both W boards, written in PicoBoot-DX. Partly superseded (see its note) |
| [boot-rom.md](boot-rom.md) | 2026-11-08 | RP2350 boot ROM: partition A/B, TBYB, `rom_chain_image()`, UF2 routing, boot time, reset reasons |
| [sizes.md](sizes.md) | 2026-11-08 | Measured firmware sizes with lwIP and the Wi-Fi blob moved out of the image |
| [hardware.md](hardware.md) | 2026-11-08 | Pico 2 W power, the console's 3.3 V rail, boot timing, range |
| [ecosystem.md](ecosystem.md) | 2026-11-08 | Payload sizes, upstream and forks, OTA projects, web building blocks, lessons |

## What we already run: PicoBoot-DX v0.10

- **Code:** about 10 KB of C and PIO (`~/GitHub/PicoBoot-DX`), plus a built-in 60 KB gekkoboot.
- **What it does:**
  - boosts the clock to 250 MHz;
  - skips 224 CS pulses, then streams the payload into the PIO with DMA;
  - releases the data line and drops the clock to 125 MHz.
- **Payload:** `IPLBOOT` header and `PICO` footer, at `0x10080000` (1.5 MB). If it's invalid, the built-in gekkoboot boots instead.
- **Tests:** a PIO simulator and tests for the tools, run in CI on SDK 2.3.1.
- **What carries over:** the PIO programs, `pio.c`, the payload check and the tools are small and proven, so they're worth reusing nearly as they are. The rest (start-up, LED, layout) changes anyway.

## What the research changes

1. **The payload area must stay 1.5 MB at `0x80000`.**
   - Swiss r2119 is 1.29 MiB, so 1 MB is 301 KB too small.
   - `0x10080000` is where every existing payload UF2 lands.
2. **The Pico 2 W has 4 MB of flash, not 2 MB.** The upper 2 MB is free for slots, the Wi-Fi blob and a second payload.
3. **A partition table breaks USB payload installs.**
   - Once a partition exists, the boot ROM puts every non-absolute UF2 into a partition, so Swiss and gekkoboot UF2s would no longer install over USB.
   - The ROM's A/B also erases one image whenever you switch.
   - A small loader of our own with `rom_chain_image()`, and no partition table, keeps USB payloads working and allows free switching.
4. **256 KB slots are enough.** The stand-in firmware is 165 KB with the Wi-Fi blob outside the image. The blob is 225–233 KB.
5. **Boot timing is the biggest unknown.** It isn't measured anywhere, and upstream reports point at the clock and flash speed. A loader adds a little time before the PIO starts, so it has to be measured early.
6. **The radio is probably powered via 3V3,** but at full Tx power it can draw more than the 3V3 guidance. Tx power control and an auto-off timer are worth having.

## Build or reuse

| Part | Choice | Why |
|---|---|---|
| Injection (PIO, DMA, payload check) | Reuse from PicoBoot-DX | Small, tested on hardware, simulator in CI |
| Slot switching | Build: a small loader using the boot ROM's `rom_chain_image()` | ROM A/B breaks USB payloads and can't switch freely; pico_fota_bootloader copies at boot |
| Wi-Fi, TCP/IP, DHCP/DNS | Reuse: SDK cyw43_arch + lwIP, the pico-examples `access_point` servers | Standard, measured to fit |
| Web server | Reuse: lwIP httpd | POST support, static files in flash; small |
| Web UI and JSON API | Build | Nothing suitable exists |
| Wi-Fi blob loading | Build a small hook at a fixed address | The SDK's version needs a partition table |
| Payload tools (`.dol` → payload, UF2) | Reuse PicoBoot-DX `tools/` | Proven, tested |
