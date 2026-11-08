# Wi-Fi OTA for PicoBoot on Pico W / Pico 2 W: research

Research done on 2026-11-07, in the PicoBoot-DX project (`~/GitHub/PicoBoot-DX`). Nothing has been implemented or decided yet.

> **Update 2026-11-08:** the follow-up research in this folder corrects three points:
> - **USB payload UF2s:** with a partition table, existing payload UF2s only work after repacking with the absolute family (and RP2350-E10 hits A2 chips). See [boot-rom.md](boot-rom.md) §5.
> - **1 MB payload area:** too small for Swiss r2119. See [ecosystem.md](ecosystem.md) §2.
> - **Pico 2 W flash:** it has 4 MB, not 2 MB.
>
> The Pico W sections are out of scope for PicoBoot-WiFi.

**Goal:** the user connects to the board's Wi-Fi and gets a simple page with:
- the board's status,
- an update page to upload a new firmware OTA image,
- a way to switch between the old and the new firmware (slot A/B).

Other useful features are listed below.

**Short answer:** it's possible on both W boards, but the effort is very different.
- **Pico 2 W (RP2350):** A/B slots with automatic rollback are built into the chip's boot ROM.
- **Pico W (RP2040):** has none of that, so it needs a bootloader of its own, and that costs space in the payload area.
- **Status page and payload upload:** easy on both boards, with no change to the flash layout.

## Constraints specific to PicoBoot

1. **Wi-Fi only after the injection.** The Pico has to be counting CS pulses before the console's first IPL read, and starting the CYW43 takes far longer than that. Nothing slow may run before the PIO state machines start. PicoBoot-DX W builds already call `cyw43_arch_init()` (for the LED) only after the PIO is running.
2. **A reboot while the console is on must not inject again.** After an OTA reboot, a try-before-you-buy rollback or a watchdog reset, the console is already running. The firmware would count 224 CS pulses from later RTC/SRAM/font reads and then drive the data line. In the PIO simulator it then stalls with the line driven, because the data runs out in the middle of a transfer. Inject only after a power-on reset (`VREG_AND_CHIP_RESET` `CHIP_RESET.HAD_POR` on RP2040, `POWMAN_CHIP_RESET.HAD_POR` on RP2350); otherwise go straight to Wi-Fi mode.
3. **The payload address is a contract.** Upstream gekkoboot's `buildtools/dol2ipl.py` writes payload UF2s to `0x10080000` with the RP2040 family ID (`0xE48BFF56`). Upstream PicoBoot's "universal" payload UF2s add RP2350-arm-s blocks (`0xE48BFF59`) for the same address. Any new layout has to keep the payload at `0x10080000`.
4. **Power and range are unknown and need measuring on hardware.**
   - **Power:** in the console the Pico is powered through its 3V3 pin. The CYW43439 is very likely on that rail too, since VSYS can go as low as 1.8 V, but check it: if a W board's LED blinks inside the console, the radio has power.
   - **Range:** Wi-Fi range from inside a closed console, with the metal shielding, is unknown.
   - **Current:** the extra current on the console's 3.3 V rail is unknown. The Pico W datasheet recommends keeping the 3V3 load under 300 mA.

## Pico 2 W: use the boot ROM

**How it works:**
- **Layout:**
  - Slot A at the start of flash, after the partition table sector.
  - The payload area unchanged at `0x80000`–`0x200000`, left outside any partition.
  - Slot B in the upper 2 MB, which PicoBoot-DX doesn't use.
  - The Wi-Fi firmware in its own partitions (`pico_use_wifi_firmware_partition()`, Pico 2 W only, SDK PR #1969). That takes the ~250 KB Wi-Fi blob out of the firmware image, so an OTA image shrinks from ~320 KB to roughly 100 KB.
- **Upload:** the firmware asks the boot ROM which slot is free (`rom_get_uf2_target_partition()`), writes the image there, and reboots in "flash update" mode (`rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, ...)`).
- **Try before you buy (TBYB):** a firmware built with `PICO_CRT0_IMAGE_TYPE_TBYB=1` has 16.7 s to call `rom_explicit_buy()`. Otherwise the board reboots into the old image (RP2350 datasheet §5.1.17, p333–334).
- **Example code:** pico-examples `pico_w/wifi/ota_update` does almost exactly this, with a TCP server on port 4242 and a Python push script instead of a web page.

**Catches:**
- **Switching between A and B is really a rollback.** The boot ROM always boots the higher version. Switching to the older slot counts as a downgrade, and the ROM erases the first sector of the newer slot to make it stick (§5.1.16, p333). To switch back and forth freely you'd need a small loader of our own: the "partition table in image" pattern (§5.1.14, p332) plus `rom_chain_image()`.
- **The payload has to be read differently.** Firmware booted from a partition sees only that partition at `0x10000000`, and reading past it causes a bus fault (§5.1.19, p335). So `0x10080000` can't be read directly. Read the payload through the untranslated window (`XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE` = `0x1c000000` + offset), or map it with the spare ATRANS2/ATRANS3 windows. Check that the DMA can read it fast enough.
- **USB payload installs change.** Once a partition table exists, a UF2 that isn't "absolute" lands in a partition, at partition start + (UF2 address − `0x10000000`) (§5.5.3, p366). So existing RP2350 payload UF2s for `0x10080000` would end up in a firmware partition, not in the payload area. Payloads would have to go through the web page, or be repacked with the "absolute" family ID (`0xE48BFF57`).
- **Installing changes.** The first install takes three UF2 files over USB: the partition table (`picotool partition create`), the Wi-Fi firmware and the application.
- **Boot time.** Hashing or signing images makes the boot ROM do more work before our code runs. Measure the time from power-on to the PIO being ready before turning that on. Signed images need permanent OTP fuses, so avoid them.

## Pico W: needs its own bootloader

The RP2040 has no A/B support in its boot ROM and always boots from `0x10000000`.

| Option | Assessment |
|---|---|
| [pico_fota_bootloader](https://github.com/JZimnol/pico_fota_bootloader) | Supports Pico W and Pico 2 W, has a 36K bootloader, swaps images with commit/rollback (`pfb_firmware_commit()`), and checks images with SHA-256. Its linker scripts would need adapting to keep `0x10080000`. |
| [picowota](https://github.com/usedbytes/picowota) | Wi-Fi in the bootloader, so the CYW43 firmware is stored twice. Too big for 2 MB with a payload area. |
| Own small loader | Two slots, each built for its own address, and a flag picks the slot. Switching is instant, but every release needs two builds. |

**Flash budget (2 MB), keeping the payload at `0x10080000`:**

| Region | Address range | Size |
|---|---|---|
| Loader | start of flash | ~40K |
| Slot A | `0x1000A000`–`0x10080000` | 472K |
| Payload | `0x10080000`–`0x10180000` | 1 MB (down from 1.5 MB) |
| Slot B | after the payload | 472K |
| Settings | the rest | |

The W firmware is 326 KB today; with lwIP and a web server I'd estimate around 400 KB, so it fits. Check that the biggest payload you care about (Swiss) fits in 1 MB. `pico_use_wifi_firmware_partition()` doesn't work on RP2040.

## Possible features

| Feature | Effort | Notes |
|---|---|---|
| Access point + captive portal (the page opens by itself on a phone) | Low | pico-examples `access_point` / `access_point_wifi_provisioning` have DHCP and DNS server code |
| Status: version, build, board, active slot, version in the other slot, payload info, uptime, temperature | Low | |
| Payload management: upload a `.dol` or payload `.uf2`, delete it | Low | Brick-proof: an invalid payload falls back to the built-in IPL |
| Several payloads stored, pick which one boots | Low–medium | gekkoboot is about 60 KB |
| "Boot the original IPL next time" switch | Low | For changing the console's clock or language settings |
| Wiring/boot diagnostics: CS/CLK seen, all transfers delivered, time to first CS | Medium | Useful for troubleshooting installs |
| Backup: download the firmware, payload and settings | Low | |
| Settings: SSID/password, Wi-Fi auto-off timer, LED behaviour | Low | |
| Join home Wi-Fi + mDNS (`picoboot.local`) | Low–medium | `pico_lwip_mdns` |
| JSON API + a command-line upload tool | Low | |
| Checking GitHub for updates on the board itself | High | Needs HTTPS (mbedTLS); not worth it |
| Upload checks: board type (W vs 2 W), size, SHA-256 | Low | |

**SDK building blocks:**
- `pico_lwip_http` (lwIP httpd, POST upload support) with `pico_set_lwip_httpd_content()` for the web files.
- `pico_lwip_mdns` for the `picoboot.local` name.
- `flash_safe_execute()` for flash writes while Wi-Fi is running.
- `cyw43_arch_enable_ap_mode()` for the access point.

Whatever goes wrong, the board can't be permanently bricked: USB BOOTSEL is in ROM on both chips, so the worst case is opening the console.

## Suggested order

1. **Hardware checks:** Wi-Fi range from a closed console, the W-board LED inside the console, and current draw.
2. **Phase 1, both W boards:** access point, status page, payload management, and the power-on-only injection rule. No layout changes and low risk.
3. **Phase 2:** firmware OTA on the Pico 2 W through the boot ROM.
4. **Phase 3:** a bootloader for the Pico W, or decide it isn't worth losing a third of the payload area.

## Reference: PicoBoot-DX (`~/GitHub/PicoBoot-DX`, v0.10)

**Build environment:**
- **SDK and tools:** Pico SDK 2.3.1 and picotool 2.3.1.
- **CI:** ubuntu-26.04 (Arm GCC 14.2).
- **Linker:** the flash layout is set in `linker/memory_extra.incl`, applied with `pico_add_linker_script_override_path()`.

**Firmware sizes (`.bin`):**

| Board | Size (bytes) |
|---|---|
| pico | 71,960 |
| pico_w | 326,416 |
| pico2 | 70,404 |
| pico2_w | 322,804 |

**Flash layout:**
- **Firmware:** `0x10000000`–`0x10080000` (512K).
- **Payload:** `0x10080000`–`0x10200000` (1.5 MB).
- **Flash size:** set to 2 MB on every board, including the 4 MB Pico 2 boards.

**Pins:** CS GP4, CLK GP5, DATA GP6.

**Boot sequence** (`src/picoboot-dx.c`, `src/picoboot-dx.pio`):
1. The clock is boosted to 250 MHz.
2. `on_transfer` skips 224 CS pulses.
3. `clocked_output` skips 32 address bits, then sends 8192 bits per CS transfer, MSB first, fed from the TX FIFO by DMA.
4. It holds the last bit until CS goes high, then releases the data line (`disable_chip`).
5. `main()` waits for the PIO to reach `disable_chip`, then lowers the clock to 125 MHz.
6. W boards use `CYW43_PIO_CLOCK_DIV_INT=4`, which keeps the CYW43 SPI clock within spec at 250 MHz.

**Payload format:**
- **Flash payload:**
  - A 32-byte header: `"IPLBOOT "` followed by the big-endian total size.
  - The scrambled image.
  - A `"PICO"` footer.
  - It's sent with DMA byte swap, rounded up to whole 1K transfers.
- **Built-in `ipl[]`:**
  - Generated by `tools/process_ipl.py`.
  - No header or footer; 0x20 flush bytes, then the image, padded to 1K (60 transfers).
- **Padding:** payloads must be whole 1K transfers. Otherwise the PIO stalls in the middle of a transfer and keeps driving the data line.

**Tests:**
- **Tools:** `tests/` holds the Python standard-library tests for the tools.
- **PIO simulator:** `tests/pio_sim.py` runs the real PIO programs against a simulated console. Useful to check that Wi-Fi changes don't affect the injection.

## Sources

- [pico-examples: pico_w/wifi/ota_update](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/ota_update)
- [pico-examples: access_point_wifi_provisioning](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/access_point_wifi_provisioning)
- [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf) §5.1.7, §5.1.14–5.1.19, §5.5.3
- [Pico W datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf), [Pico 2 W datasheet](https://datasheets.raspberrypi.com/picow/pico-2-w-datasheet.pdf)
- [pico-sdk PR #1969: Wi-Fi firmware partition support](https://github.com/raspberrypi/pico-sdk/pull/1969), [pico-sdk issue #2691](https://github.com/raspberrypi/pico-sdk/issues/2691)
- [pico_fota_bootloader](https://github.com/JZimnol/pico_fota_bootloader), [picowota](https://github.com/usedbytes/picowota)
- [gekkoboot dol2ipl.py](https://github.com/redolution/gekkoboot/blob/master/buildtools/dol2ipl.py)
