# RP2350 boot ROM: A/B slots, chaining, UF2 routing, boot time

Research done on 2026-11-08 for PicoBoot-WiFi (Pico 2 W only).

**Question:** should firmware A/B use the boot ROM's partition table with try-before-you-buy (TBYB), or a small loader of our own that picks a slot and calls `rom_chain_image()`?

**Short answer:** a loader of our own, with no partition table, is the better fit.
- **USB payloads:** without a partition table, existing payload UF2s (Swiss, gekkoboot) keep working over USB. With one, they don't (§5).
- **Switching:** the loader can switch freely between two good images. The ROM's A/B always erases one of them when you switch (§3).
- **Costs:**
  - one extra ROM image scan at boot (§6);
  - the Wi-Fi firmware needs a fixed-address loader instead of the SDK's partition support (see [sizes.md](sizes.md));
  - the loader itself can only be updated over USB.

**Sources:**
- **DS:** RP2350 datasheet, build 80d627281ed5 (2026-01-10); page numbers are the printed ones.
- **BR:** pico-bootrom-rp2350 at tag A4 (c6cdb17). The A/B, ATRANS and UF2 code is the same as at A2.
- **SDK:** pico-sdk 2.3.1.

**[inf]** marks a conclusion drawn from the code rather than read in the docs.

## 1. Partition table

- **Where it lives:** a PARTITION_TABLE block in a block loop that starts in the first 4 KB of flash (slot 0). Slot 1 is the next 4 KB and holds an optional second copy (DS §5.1.15 p.332).
  - The *singleton* flag stops the ROM from scanning slot 1 (p.333).
  - Changing the slot size takes an OTP fuse. On A2/A3 chips that fuse can brick the chip (erratum E18, p.1352).
- **Size and limits:**
  - The block is at most 0x280 bytes.
  - At most 16 partitions, each a whole number of 4 KB sectors.
  - Bootable partitions must lie in the first 16 MB (§5.1.2.1 p.327).
  - Each partition can accept up to 3 extra UF2 family IDs.
- **First partition:** picotool puts it at 0x2000 by default. picotool only checks 4 KB alignment, not overlaps.
- **Unpartitioned space:** has its own permission and family flags. It is the only place the *absolute* family can be accepted (Table 502 p.388).

## 2. A/B on a normal boot

- **Version compare:** the IMAGE_DEF VERSION item is compared as (rollback).major.minor. A missing rollback counts as 0 (§5.1.6 p.329, p.382).
- **Ties:** B wins only with a strictly greater version, so equal versions boot A (BR `varm_flash_boot.c:238`).
- **Unbought TBYB images:**
  - "a non TBYB image will always be chosen over a TBYB image … during a normal non-FLASH_UPDATE boot" (p.333).
  - The launcher refuses a TBYB image unless it's the flash-update target (`varm_launch_image.c:214`).
- **FLASH_UPDATE reboot:** `rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, …, XIP_BASE + partition start)` prefers that partition "irrespective of version", as long as it verifies. If it doesn't verify, the other image boots (p.333).
- **`rom_explicit_buy()`** (`varm_launch_image.c:147-190`):
  1. stops the watchdog;
  2. erases and rewrites the 4 KB sector holding the IMAGE_DEF, with the TBYB bit cleared;
  3. erases the first sector of the other partition, if a downgrade marked it.
- **Timeout:** 16.7 s ("24-bit count on a 1-microsecond timebase", p.334). After that comes a normal boot, which skips the unbought image.

## 3. Switching between two good images

- **What the ROM erases** (`varm_flash_boot.c:240-327`): on a FLASH_UPDATE boot whose target verifies, the ROM erases the first 4 KB of the other partition when the other image would otherwise win.
  - **Target A:** B is erased if B's version is strictly greater.
  - **Target B:** A is erased if A's version is greater **or equal**.
- **When:**
  - For a non-TBYB image, the erase happens at launch (L449).
  - For a TBYB image, it waits for `explicit_buy`.
  - §5.1.17 says the other partition is always erased on a buy; the code and the `explicit_buy` text don't.
- **No way around it:** with a partition table there is no way to toggle between two good images without an erase.
  - No version items, or equal versions: A always wins, and selecting B erases A.
  - TBYB images never boot on a normal boot.
  - No OTP flag changes the A/B choice.
- **A chained image never triggers an erase.** `chain_image()` with a positive base never sets the downgrade-erase address; only the A/B choice does. Checked in BR: `version_downgrade_erase_flash_addr` is set only in the A/B code.

## 4. `rom_chain_image()`

- **No partition table needed:** it searches any word-aligned window for a block loop that starts in the window's first 4 KB (p.352).
  - A normal flash image at offset 0 can chain an image at any 4 KB-aligned offset.
  - This is how a loader of our own would work.
- **Address translation** (BR `varm_apis.c:529-583`, `varm_launch_image.c:224-252`; the same code that boots partitions):
  - ATRANS0–3 are set so `0x10000000` maps to the window, up to 16 MB.
  - Unused panes get size 0 and bus-fault on access (p.335).
  - ATRANS4–7 are left alone.
  - Every slot image can be **the same binary**, linked at `0x10000000`.
  - The window must start on a 4 KB boundary and stay inside `0x10000000`–`0x11FFFFFF`.
- **Work area:** the datasheet says 3064 bytes and the SDK header says 3264. Use 4 KB.
- **TBYB:** a negative `window_base` marks the window as a "flash update" target, which enables TBYB even without a partition table [inf; untested].
- **Reading the rest of flash from a chained image:**
  - **Untranslated window:** `XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE` (`0x1c000000`) + storage offset (§4.4.1 p.315). Secure code may use it at reset. The SDK's cyw43 driver reads its firmware partition this way (`cyw43_driver.c:181-188`).
  - **Spare pane:** reprogram one of the spare ATRANS1–3 panes to map another part of flash ("could be used by the application", p.335), then flush the XIP cache (§12.14.4.2 p.1144).
  - **Flash writes:** the SDK's `flash_range_erase`/`flash_range_program` take untranslated storage offsets.

## 5. UF2 routing over USB (BOOTSEL)

**Key finding** (BR `varm_nsboot.c:544`; checked in the source): the ROM uses absolute addressing only when `!pt->partition_count || family == ABSOLUTE`. Once the table has a partition, every other family is routed into a partition.

- **No partition table:**
  - UF2 address `0x10000000` maps to flash offset 0.
  - Accepted families: absolute, data, rp2350-arm-s and rp2350-riscv. **rp2040 is not accepted** (`varm_flash_boot.c:864-881`).
  - Existing "universal" payload UF2s (rp2040 + rp2350-arm-s blocks at `0x10080000`) install correctly: the RP2040 blocks are ignored.
- **With a partition table:**
  - Address `0x10000000` maps to the partition start.
  - A block at or past the partition end gives `UF2_STATUS_ABORT_BAD_ADDRESS`. The transfer stops half written and the board doesn't reboot (`usb_virtual_disk.c:680-690`).
  - An rp2350-arm-s payload at `0x10080000` would therefore land 512 KB into a firmware partition, or abort.
- **Workarounds, and why they fall short:**
  - **Repack with the absolute family:** works, but every Swiss and gekkoboot release would need repacking.
  - **A non-bootable partition from sector 0 to the end of the payload, accepting rp2350-arm-s:** might put payloads at absolute `0x80000` [inf, untested]. But a stock app UF2 would then overwrite the partition table.
  - **Both are hit by RP2350-E10 on A2 chips:** "UF2 drag-and-drop doesn't work with partition tables" unless the UF2 carries picotool's absolute workaround block (p.1351). Third-party payload UF2s don't carry it, and Pico 2 W boards ship as A2, A3 or A4.
- **Mixed families in one file:** blocks of a family that isn't accepted are ignored. Only one family in a file should be accepted (§5.5.3.2).

## 6. Boot time

- **What the ROM does first** (Table 462 pp.337-340):
  - clears boot RAM;
  - generates a boot random number: 100 SHA blocks, about 1 ms on A3+ and about 4 ms on A2 [inf];
  - checks the boot vectors, powers up SRAM, checks BOOTSEL and OTP;
  - flash setup: up to 16 read-mode tries, each scanning ≤4 KB (Table 463 p.343);
  - reads the partition table, if there is one, then scans each candidate's first 4 KB.
- **Hash checks:** if the image has a HASH_VALUE, the ROM hashes the whole load map, so the cost grows with image size (`varm_blocks.c:856`). TBYB only adds arming the watchdog.
- **A chained image:** pays one more scan and verify on top of the loader's own boot.
- **Published numbers:** none from Raspberry Pi.
  - A third-party scope measurement on an A4 board: about 10 ms out of reset (the supply ramp), about 5 ms of ROM to the application, then 1–3 ms to the main loop (piers.rocks, 2025-08-25).
  - A forum measurement: 34 ms to the first pin toggle on a Pico 2 running MicroPython.
- **Power-on reset:** released 3–10 µs after DVDD rises above about 0.95 V (Tables 570-571 pp.462-465).

## 7. Reset reason and registers that survive

- **POWMAN `CHIP_RESET` bits:** HAD_POR (16), HAD_BOR (17), HAD_RUN_LOW (18), HAD_DP_RESET_REQ (19), HAD_RESCUE (21), HAD_WATCHDOG_RESET_* (22–24, 28), HAD_SWCORE_PD (25), HAD_GLITCH_DETECT (26) (pp.426-428).
- **SDK helpers:**
  - `rom_get_last_boot_type()` gives NORMAL, BOOTSEL, RAM_IMAGE, FLASH_UPDATE or PC_SP, plus a CHAINED flag.
  - `watchdog_caused_reboot()` tells a watchdog reset apart.
- **What `rom_reboot()` resets:** everything except PROC_COLD, and it writes p0/p1 to watchdog SCRATCH2/3.
- **Watchdog SCRATCH0–7:**
  - survive watchdog reboots;
  - are reset by POR, BOR and RUN (§12.9.5 p.1106);
  - SCRATCH2–7 are used by the ROM and the SDK, so only **SCRATCH0–1** are free.
- **POWMAN SCRATCH0–7 and BOOT0–3:** lost only on POR, BOR and RUN (Table 560 p.451).
- **Datasheet contradiction:** Table 560 and §12.9.5 disagree on whether the watchdog scratch registers survive. Test it on hardware.

## 8. Flash

- **Part:** the Pico 2 W schematic shows **W25Q32RV, 4 MB**. The datasheet text says "W25Q16JV, 2 MB", a copy error; the SDK board file says 4 MB.
- **Timing:** 0.25 ms per page program, 30 ms per sector erase, 120 ms per 64 KB block erase (typical figures, Winbond RV brief). The maximum sector-erase time wasn't found.
- **Writes while running from flash:**
  - While an erase or program runs, XIP accesses bus-fault.
  - `flash_safe_execute()` keeps interrupts and the other core off flash.
  - In SDK 2.3.1, `rom_flash_op` and `rom_explicit_buy` already use it.

## Needs a hardware test

1. **Boot time:** reset to `main()` and to "PIO ready" on a Pico 2 W, for a plain image, a loader plus chain, and partition-table A/B. Repeat with a warm board.
2. **Reset bits after a reboot:** whether HAD_POR is cleared by a watchdog or `rom_reboot()` reset, and which scratch registers survive.
3. **TBYB through `chain_image`:** with a negative base and no partition table.
4. **Payload reads:** DMA throughput from the untranslated XIP window, compared with the console's EXI data rate.
5. **Chip revisions:** the mix of A2, A3 and A4 chips among Pico 2 W boards (E10 and the ROM clock speed differ).
