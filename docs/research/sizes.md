# Firmware sizes on the Pico 2 W

Measured on 2026-11-08 for PicoBoot-WiFi.

**Question:** does the firmware fit a 256 KB slot once the Wi-Fi firmware blob is moved out of the image?

**Short answer:** yes, with about 90 KB to spare.
- **Release build:** a stand-in for the real firmware measures **165,240 bytes**. It has the PicoBoot-DX core, the built-in 60 KB gekkoboot, an access point with DHCP/DNS, lwIP httpd with POST and a flash writer.
- **MinSizeRel build:** 142,428 bytes.
- **What's still missing:** the real web UI. It adds roughly its own (gzipped) size.

## How it was measured

- **Container:** Docker, ubuntu:24.04 aarch64, arm-none-eabi-gcc 13.2.1, cmake 3.28.3.
- **Sources:** pico-sdk 2.3.1 (cyw43-driver 055d642, lwIP 77dcd25), picotool 2.3.1, pico-examples `sdk-2.3.1`; `PICO_BOARD=pico2_w`.
- **Calibration:** PicoBoot-DX v0.10 pico2_w built to 322,796 bytes here, against 322,804 in its CI. The toolchains agree.
- **What was measured:** compiled and linked sizes only. Nothing was run on hardware.
- **Raw data:** the size table, `.map` breakdowns and the exact commands are in the session scratchpad (`sizes/`), not in the repo.

## Results (`.bin` bytes)

| Target | Release | MinSizeRel |
|---|---|---|
| pico-examples `ota_update` (Wi-Fi firmware partition, TBYB) | 94,372 | 73,252 |
| pico-examples `httpd`, blob embedded | 350,592 | 322,744 |
| pico-examples `httpd`, blob in a partition | 129,700 | 101,644 |
| pico-examples `access_point` (poll), blob embedded | 321,688 | 300,148 |
| pico-examples `access_point` (poll), blob in a partition | 97,524 | 75,760 |
| PicoBoot-DX v0.10 as it is, blob in a partition | 101,920 | 92,684 |
| **Stand-in for the real firmware** (poll, blob in a partition) | **165,240** | **142,428** |
| Same, `threadsafe_background` instead of poll | 167,744 | 144,412 |
| Same, blob embedded | 389,568 | 366,964 |
| Same + USB stdio and printf | 178,732 | 153,332 |
| Same + `LWIP_NOASSERT` | 156,112 | 133,292 |

**Where the 165,240 bytes go (Release, from the `.map` file):**

| Part | Bytes |
|---|---|
| Built-in gekkoboot (`ipl[]`) | 61,440 |
| lwIP core | 37,708 |
| SDK runtime | 16,267 |
| cyw43-driver | 11,674 |
| httpd + test pages | 9,143 |
| String literals (about 4.5 KB of it is lwIP assert text) | 8,254 |
| newlib | 5,773 |
| SDK cyw43 glue, PIO SPI | 5,396 |
| DHCP/DNS server | 1,983 |
| Web/OTA stand-in code | 1,820 |
| PicoBoot core | 1,186 |
| Flash and boot ROM wrappers | 1,160 |
| Other (libgcc, NVRAM, async_context) | 2,127 |

Without the built-in gekkoboot, the networking firmware is about 104 KB.

## The Wi-Fi firmware blob

| Blob | Firmware | CLM | Total |
|---|---|---|---|
| Wi-Fi only (`w43439A0_7_95_49_00_combined.h`) | 224,190 | 984 | 225,240 |
| Wi-Fi + Bluetooth (`wb43439A0_…`) | 231,077 | 984 | 232,408 |

- **SDK partition image:** `pico_use_wifi_firmware_partition()` always packs the Wi-Fi + Bluetooth blob. Its `.bin` is 232,596 bytes and its UF2 465,920 bytes. UF2 family `cyw43-firmware` = `0xE48BFF55`.
- **How the SDK finds the blob:** it looks it up through the partition table (`rom_get_partition_table_info`, then `rom_pick_ab_partition_during_update`) and reads it through the untranslated XIP window (`cyw43_driver.c:117-198`). Without a partition table that lookup fails.
- **A fixed flash address instead:**
  - The driver gets the blob from `CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE` (`cyw43_ll.c:67`). That file defines `CYW43_WIFI_FW_LEN`, `CYW43_CLM_LEN` and `fw_data`, and the macro can be overridden (`cyw43_configport.h:65`).
  - A few lines pointing `fw_data` at `XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset` do the job. It must be the untranslated address, because `XIP_BASE` is remapped when running from a slot.
  - Not prototyped yet; about half a day of work.

## Other SDK facts found along the way

- **pico-examples `ota_update` partition table:**

  | Partition | Range | Size |
  |---|---|---|
  | Partition table | `0x0`–`0x2000` | 8K |
  | Main A | `0x002000`–`0x1b6000` | 1744K |
  | Main B | `0x1b6000`–`0x36a000` | 1744K |
  | Wi-Fi firmware A | `0x36b000`–`0x3ab000` | 256K |
  | Wi-Fi firmware B | `0x3ab000`–`0x3eb000` | 256K |

  picotool starts the first partition at `0x2000` unless told otherwise.
- **`flash_safe_execute()`:**
  - Without pico_multicore it only disables interrupts on the calling core.
  - cyw43_arch doesn't touch flash. In poll mode the httpd callbacks run in thread context, so flash writes from an upload handler are straightforward.
  - In `threadsafe_background` mode Wi-Fi stalls while a sector is erased.
  - The DMA must not read flash while flash is being erased or programmed.
- **Signatures:** `rom_chain_image(workarea, size, region_base, region_size)` needs a work area of 3,264 bytes or more and doesn't return on success. `rom_reboot()` needs `boot_picoboot_headers` linked.
