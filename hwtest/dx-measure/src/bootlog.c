/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "bootlog.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

// 16K at 0x3F0000, in the Pico 2 W's upper 2 MB, which PicoBoot-DX doesn't use (its firmware
// and payload end at 2 MB). Four sectors of 16 records each, used as a ring. Kept away from the
// last sector of flash, which picotool's RP2350-E10 UF2 block may overwrite (see report.c).
#define BOOTLOG_OFFSET      0x3F0000u
#define BOOTLOG_SECTORS     4u
#define SWITCH_OFFSET       (BOOTLOG_OFFSET + BOOTLOG_SECTORS * FLASH_SECTOR_SIZE)
#define SWITCHES_MAGIC      0x53575431u      // "SWT1"
#define RECORDS_PER_SECTOR  (FLASH_SECTOR_SIZE / sizeof(boot_record_t))
#define BOOTLOG_SLOTS       (BOOTLOG_SECTORS * RECORDS_PER_SECTOR)

static const boot_record_t *slot(uint i) {
    return (const boot_record_t *)(XIP_NOCACHE_NOALLOC_BASE + BOOTLOG_OFFSET + i * sizeof(boot_record_t));
}

static uint32_t crc32(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
        }
    }
    return ~crc;
}

static bool valid(const boot_record_t *rec) {
    return rec->magic == BOOTLOG_MAGIC && rec->crc == crc32(rec, offsetof(boot_record_t, crc));
}

static bool erased(const boot_record_t *rec) {
    const uint32_t *w = (const uint32_t *)rec;
    for (uint i = 0; i < sizeof(*rec) / sizeof(*w); i++) {
        if (w[i] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return true;
}

void bootlog_append(boot_record_t *rec) {
    // Find the newest record; the next slot after it is where this one goes
    uint32_t newest_seq = 0;
    int newest = -1;
    for (uint i = 0; i < BOOTLOG_SLOTS; i++) {
        if (valid(slot(i)) && (newest < 0 || slot(i)->seq > newest_seq)) {
            newest_seq = slot(i)->seq;
            newest = (int)i;
        }
    }
    uint next = (newest < 0) ? 0 : ((uint)newest + 1) % BOOTLOG_SLOTS;

    rec->magic = BOOTLOG_MAGIC;
    rec->seq = newest_seq + 1;
    rec->crc = crc32(rec, offsetof(boot_record_t, crc));

    uint32_t offset = BOOTLOG_OFFSET + next * sizeof(boot_record_t);
    uint32_t irq = save_and_disable_interrupts();
    if (!erased(slot(next))) {
        // Starting a sector again (or a torn write): drop that sector's older records
        flash_range_erase(offset & ~(FLASH_SECTOR_SIZE - 1), FLASH_SECTOR_SIZE);
    }
    flash_range_program(offset, (const uint8_t *)rec, sizeof(*rec));
    restore_interrupts(irq);
}

void bootlog_clear(void) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(BOOTLOG_OFFSET, BOOTLOG_SECTORS * FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}

uint32_t bootlog_switches(void) {
    const volatile uint32_t *w = (const volatile uint32_t *)(XIP_NOCACHE_NOALLOC_BASE + SWITCH_OFFSET);
    return w[0] == SWITCHES_MAGIC ? w[1] : 0;
}

void bootlog_set_switches(uint32_t switches) {
    uint32_t page[FLASH_PAGE_SIZE / sizeof(uint32_t)];
    memset(page, 0xFF, sizeof(page));
    page[0] = SWITCHES_MAGIC;
    page[1] = switches;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SWITCH_OFFSET, FLASH_SECTOR_SIZE);
    if (switches) {
        flash_range_program(SWITCH_OFFSET, (const uint8_t *)page, sizeof(page));
    }
    restore_interrupts(irq);
}

uint bootlog_read(boot_record_t *out, uint max_count) {
    uint count = 0;
    for (uint i = 0; i < BOOTLOG_SLOTS; i++) {
        if (!valid(slot(i))) {
            continue;
        }
        // Insertion sort, newest (highest seq) first
        boot_record_t rec;
        memcpy(&rec, slot(i), sizeof(rec));
        uint pos = count < max_count ? count : max_count;
        while (pos > 0 && out[pos - 1].seq < rec.seq) {
            if (pos < max_count) {
                out[pos] = out[pos - 1];
            }
            pos--;
        }
        if (pos < max_count) {
            out[pos] = rec;
            if (count < max_count) {
                count++;
            }
        }
    }
    return count;
}
