# Flash programmed from RAM only

Whether flash can be read while it's being programmed or erased, what happens when the data to program is itself in flash, and what a check in `storage.c` could do. For the plan's later-releases item "Flash is programmed from RAM only", taken up in release 1.0.2 (the plan's "Release 1.0.2"); the owner chose its scope from this note.

**Sources:**
- the firmware at `1.0.1` (`storage.c`, `payloads.c`, `updater.c`, `update.c`, `payloadup.c`, `records.c`, `ringlog.c`) and its build (`arm-none-eabi-nm`, `-size`);
- the Pico SDK 2.3.1 in the build image (`hardware_flash/flash.c`, `flash.h`);
- the RP2350 boot ROM source, [pico-bootrom-rp2350](https://github.com/raspberrypi/pico-bootrom-rp2350) at tag `A2`, the owner's chip (`varm_generic_flash.c`, `native_generic_flash.c`, `varm_checked_flash.c`, `varm_misc.S`);
- the [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), build `80d627281ed5` (page numbers as printed);
- Winbond's W25Q32RV_DTR datasheet, revision H (2025-12-15), for the RV family of the Pico 2 W's flash (page numbers as printed). Its wording in the passages quoted here is the same as in the older W25Q32FV datasheet.

Nothing here was measured on hardware, except the phase 5 hang ([phase5-results.md](phase5-results.md), the acceptance's second run).

## 1. Can flash be read while it's being written?

No. Three things each stop it:

| Part | A read during a program or erase | Source |
|---|---|---|
| The flash chip | While BUSY, "the device will ignore further instructions except for the Read Status Register and Erase/Program Suspend instruction". A Read Data issued then "is ignored". BUSY lasts 0.25 ms per page and 30 ms per sector typically, and at most 2 ms and 240 ms ([boot-rom.md](boot-rom.md) §8 lacked the maximum). `storage_flash_erase()` erases one sector at a time, so a write keeps interrupts off far less than the 8 s watchdog | Winbond: BUSY (p. 12), Read Data (03h) (p. 29), AC characteristics (p. 80) |
| The QSPI bus | One chip on one set of wires: while a command is being sent, the bus is busy with it | Pico 2 W schematic |
| The RP2350's QMI | The ROM sends each command in direct mode, where "the XIP window becomes inaccessible. Attempting to access it generates a bus fault" | Datasheet §12.14.5 (p. 1144), DIRECT_CSR.EN (p. 1149), §5.4.8.9–10 (p. 355–356) |

Three details:
- **The XIP cache still answers.** "XIP accesses first query the cache. If a cache entry is not found, the QMI generates an external serial access" (§4.4, p. 315).
  - Nothing invalidates the cache before a write. The SDK only cleans it (`xip_cache_clean_all()` in `flash_save_hardware_state()`), and the ROM's `connect_internal_flash()` and `flash_exit_xip()` don't touch it. Only `flash_flush_cache()` invalidates it, after the write.
  - So anything already in the cache (8-byte lines) can still be read during a write. This comes from the code and the datasheet; it wasn't tried.
- **Between the ROM's status polls:**
  - The ROM turns direct mode on only while it sends a command: `s_native_crit_flash_put_get()` sets DIRECT_CSR.EN and clears it again for each one, including each status read in the wait for BUSY.
  - In between, the QMI is in plain 03h read mode: `flash_exit_xip()` sets it up, and the ROM's own comment says it "leaves the QMI in the same 03h XIP configuration between programming commands" (`varm_generic_flash.c`).
  - So a cache miss that falls between two polls would most likely reach the busy chip as a 03h read. The chip ignores it, so the read returns no flash data rather than a bus fault. This comes from the ROM code: the datasheet describes the whole operation as direct mode. It wasn't tried. Either way, nothing reads flash data.
- **Suspend isn't used.** The chip could pause an erase or program to serve reads (Erase/Program Suspend, 75h, Winbond p. 53), but neither the ROM nor the SDK sends it.

## 2. What happens when the data is in flash

The ROM's `flash_range_program()` sends one 256-byte page per command. `s_native_crit_flash_put_get()` turns direct mode on, which also drives CS low, and only then reads the data, a byte at a time (`*tx++`), while the page is being sent. So the data is read in the middle of the command, in direct mode:

1. **A byte that's in the cache** is read normally.
2. **A byte that isn't** returns a bus fault. Interrupts being off doesn't stop it: PRIMASK masks interrupts, not HardFault, and BusFault, which is off by default, escalates to HardFault.
3. **The HardFault handler is in flash too.** The vector table is in RAM (`ram_vector_table`), so the core finds the handler. But the handler itself, the firmware's `isr_hardfault()`, which reboots at once, is in flash (`0x10000208` in the 1.0.1 build). Fetching its first instruction faults again, and a fault in the HardFault handler puts the core in lockup: it stops. If the handler and `watchdog_reboot()` happened to be in the cache, it would reboot at once instead.
4. **The watchdog resets the Pico** 8 s later (`WATCHDOG_MS`, `main.c`). That is the phase 5 hang. Its note says `flash_range_program()` "stalled". The datasheet says it's a bus fault, and to the eye a lockup looks the same.
5. **It depends on the cache.**
   - A `const` that only the write reads, like phase 5's zeros, misses every time and hangs every time.
   - A `const` that the firmware also reads elsewhere may be in the cache. The write then works, until the cache evicts that data.
   - So a test on the Pico can pass by luck. This is inferred from points 1 and 2; it wasn't tried.
6. **What's left in flash:**
   - The page program command and address have gone out, and CS stays low.
   - The reset releases CS at a byte boundary, and the chip programs the bytes it has received: "less than 256 bytes (a partial page) can be programmed" (Winbond, Page Program (02h), p. 46).
   - In phase 5 the first byte missed, so nothing was written. A partly cached source could write the start of a page.
   - The readers reject a torn page like any broken one. The records and the boot log check a CRC. An update and a payload upload program their first sector, which holds the header, last, so a hang before that leaves no valid header, and the boot checks the header (`payload_check()`, the ROM's `rom_chain_image()`). The zeros of `invalidate()` are meant to break a header anyway.
   - This is derived from the datasheets; it wasn't tried.

## 3. What programs flash today

Every write goes through `program()` in `storage.c`, which calls the SDK's `flash_range_program()` with interrupts off:

| Writer | Data from | In |
|---|---|---|
| Boot control and settings (`records.c`) | `static uint8_t image[RECORD_SECTOR_SIZE]` | RAM (`.bss`) |
| Boot log (`ringlog.c`) | `uint8_t page[RINGLOG_ENTRY_SIZE]`, a local | RAM (stack) |
| Firmware update (`update.c`, via `updater.c`) | `sector`, `first`, `blob_first` in the `update_t` writer | RAM (`__uninitialized_ram`) |
| Payload upload (`payloadup.c`, via `payloads.c`) | `sector`, `first` in the `payloadup_t` writer | RAM (`__uninitialized_ram`) |
| A payload's invalidation (`invalidate()`, `payloads.c`) | `zeros`, since `df962b8` | RAM (`__uninitialized_ram`) |

The rest of a write is already off flash:
- The SDK's flash functions run from RAM (`flash_range_program` at `0x2000f43c`).
- Core 1 is never started (no `pico_multicore`).
- Interrupts are off.
- The injection's DMA is done before any write (design C6).

So 1.0.1 is fine.

## 4. Nothing checks it

- **The SDK:** `flash.h` warns about the other core and about interrupt handlers in flash, but says nothing about where `data` is.
- **The ROM and the datasheet:** the datasheet's `flash_range_program()` (§5.4.8.11) says nothing either. The ROM's checked `flash_op()` checks the flash address range, not the buffer.
- **The host tests:** their flash is a plain array (`fake_program()` in `tests/host/test_records.c`, the `feed_*.c` helpers), so data from anywhere works there.
- **Hardware tests:** they can pass by luck (section 2, point 5).

The rule lives only in the comment in `storage.h` and in code reviews.

## 5. What a check could do

Where: in `program()`, before interrupts go off. Reading flash is fine at that point.

| Option | With data in flash | Cost |
|---|---|---|
| **Copy through a RAM page** | Works: each page is copied into a 256-byte RAM buffer, then programmed from it | 256 bytes of RAM (the firmware uses about 81 KB of 512 KB), and one address check per write. The copy runs only for data outside SRAM, which no writer has today |
| Refuse | The write is lost without a word. `program()` returns nothing, so reporting it means changing `record_flash_t`, `update_io_t` and `payloadup_io_t` | Small, but touches three interfaces |
| `panic()` | Resets at once, rather than after 8 s. The write still fails | Almost none |
| Keep the rule (today) | Hangs for 8 s, then resets; maybe a partial page | None |

The copy might look like this (a sketch, not reviewed):

```c
static void program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    const uint32_t at = (uint32_t)(uintptr_t)ctx + offset;
    const uintptr_t from = (uintptr_t)data;
    if (from >= SRAM_BASE && from + length <= SRAM_END) {
        const uint32_t irq = save_and_disable_interrupts();
        flash_range_program(at, data, length);
        restore_interrupts(irq);
        return;
    }
    // Flash can't be read while it's being programmed: copy each page into RAM first
    static uint8_t __uninitialized_ram(page)[FLASH_PAGE_SIZE];
    for (size_t done = 0; done < length; done += FLASH_PAGE_SIZE) {
        memcpy(page, data + done, FLASH_PAGE_SIZE);
        const uint32_t irq = save_and_disable_interrupts();
        flash_range_program(at + done, page, FLASH_PAGE_SIZE);
        restore_interrupts(irq);
    }
}
```

- **SRAM bounds:** `SRAM_BASE` to `SRAM_END` (`0x20000000`–`0x20082000`) covers the scratch banks too, so the stack counts as RAM.
- **The buffer's size doesn't limit a write:** the copy goes a page at a time, so any length works. A large copy from flash, such as a whole slot, makes one SDK call per page instead of one per sector. Erasing dominates (30 ms per sector against 16 × 0.25 ms to program it), so it would be a few percent slower (estimated). A 4 KB buffer would avoid that, for 4 KB of RAM.
- **`zeros`:** if it goes back to flash (`static const`, as before phase 5's fix), every payload upload and delete goes through the copy (`invalidate()` is called by both), and the RAM used stays the same.
- **A test on the Pico for the copy:** a dev build's `/test` page could program a page of `0xFF` from a `const` array over any page. Programming only clears bits, so the page doesn't change. Without the check, that write would hang (the array is read nowhere else, so it misses the cache); with it, it returns.
- **The re-test** (later-releases item 1) covers every write path: a payload upload and a delete, a firmware update, a settings save, and a boot that writes the log and boot control.

## 6. Conclusions for release 1.0.2

The owner's choices (2026-11-15): the copy through a 256-byte RAM page, the zeros back in flash so that a payload upload or delete goes through it, and a re-test of every write path.

1. **Flash can't be read while it's being programmed or erased.** The chip ignores reads while BUSY, the bus is busy, and the QMI returns a bus fault in direct mode. The one exception is data already in the XIP cache.
2. **The phase 5 hang was a bus fault and a lockup,** not a stall. The watchdog's reset is the same either way.
3. **Whether data in flash hangs depends on the cache,** so a hardware test can miss it. That makes a check in code worth more than a rule.
4. **1.0.1 is fine:** every writer programs from RAM.
5. **A check in `program()`** that copies data from outside SRAM through a 256-byte RAM page would remove the hazard. It needs no interface change and has no visible effect.
6. **With the zeros in flash, the copy has a user,** and the re-test's upload and delete prove it on the release build, on the path that hung in phase 5.
