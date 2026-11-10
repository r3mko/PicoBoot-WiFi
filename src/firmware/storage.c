/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "storage.h"
#include "layout.h"
#include "records.h"
#include "ringlog.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"

_Static_assert(LAYOUT_BOOTLOG_SIZE / RINGLOG_ENTRY_SIZE <= RINGLOG_MAX_ENTRIES, "ringlog_read() reads the whole log");

#define BOOTLOG_MAGIC   0x31474C42u     // "BLG1"
#define BOOTLOG_VERSION 3               // Bump when boot_report_t changes (2: payload_chosen; 3: loader_flags)

// Read through the untranslated, uncached XIP window: the loader maps only this firmware's slot at
// XIP_BASE, and the cache mustn't hide what was just programmed
#define FLASH_AREA(offset) ((const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + (offset)))

_Static_assert(sizeof(boot_report_t) <= RINGLOG_MAX_PAYLOAD, "a boot report must fit a log entry");

// record_flash_t callbacks; ctx is the area's flash offset
static void erase_sector(void *ctx, uint32_t offset) {
    const uint32_t irq = save_and_disable_interrupts();
    flash_range_erase((uint32_t)(uintptr_t)ctx + offset, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}

static void program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    const uint32_t irq = save_and_disable_interrupts();
    flash_range_program((uint32_t)(uintptr_t)ctx + offset, data, length);
    restore_interrupts(irq);
}

void storage_flash_erase(uint32_t offset) {
    erase_sector((void *)0, offset);
}

void storage_flash_program(uint32_t offset, const uint8_t *data, size_t length) {
    program((void *)0, offset, data, length);
}

static const record_flash_t bootctl_flash = {erase_sector, program, (void *)(uintptr_t)LAYOUT_BOOTCTL_OFFSET};
static const record_flash_t bootlog_flash = {erase_sector, program, (void *)(uintptr_t)LAYOUT_BOOTLOG_OFFSET};
static const record_flash_t settings_flash = {erase_sector, program, (void *)(uintptr_t)LAYOUT_SETTINGS_OFFSET};
static const ringlog_t bootlog = {FLASH_AREA(LAYOUT_BOOTLOG_OFFSET), LAYOUT_BOOTLOG_SIZE, &bootlog_flash};

bool storage_bootctl_load(bootctl_t *out) {
    // A record of any format: the fields this firmware knows; the ones an older record lacks are zero
    // (no payload names). A newer one's extra fields are dropped when this firmware writes (D42).
    const record_header_t *rec = record_find_any(FLASH_AREA(LAYOUT_BOOTCTL_OFFSET), BOOTCTL_MAGIC, 1,
                                                 BOOTCTL_LOADER_BYTES);
    if (!rec) {
        *out = BOOTCTL_DEFAULT;
        return false;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out, record_payload(rec), rec->length < sizeof(*out) ? rec->length : sizeof(*out));
    for (int i = 0; i < 2; i++) {
        out->payload_name[i][BOOTCTL_NAME_MAX] = '\0';     // Whatever the record held
    }
    return true;
}

bool storage_bootctl_store(const bootctl_t *b) {
    return record_write_any(FLASH_AREA(LAYOUT_BOOTCTL_OFFSET), &bootctl_flash, BOOTCTL_MAGIC, BOOTCTL_VERSION,
                            b, sizeof(*b));
}

bool storage_settings_load(settings_record_t *out) {
    const record_header_t *rec = record_find(FLASH_AREA(LAYOUT_SETTINGS_OFFSET), SETTINGS_MAGIC,
                                             SETTINGS_VERSION, sizeof(settings_record_t), NULL);
    *out = rec ? *(const settings_record_t *)record_payload(rec) : SETTINGS_RECORD_DEFAULT;
    // A stored value that isn't allowed falls back on its own default; the others stay
    settings_record_sanitize(out);
    return rec != NULL;
}

bool storage_settings_store(const settings_record_t *r) {
    settings_record_t check = *r;
    if (!settings_record_sanitize(&check)) {
        return false;
    }
    return record_write(FLASH_AREA(LAYOUT_SETTINGS_OFFSET), &settings_flash, SETTINGS_MAGIC, SETTINGS_VERSION,
                        r, sizeof(*r));
}

bool storage_bootlog_append(const boot_report_t *r) {
    return ringlog_append(&bootlog, BOOTLOG_MAGIC, BOOTLOG_VERSION, r, sizeof(*r));
}

unsigned storage_bootlog_read(boot_report_t *out, uint32_t *seq, unsigned max) {
    return ringlog_read(&bootlog, BOOTLOG_MAGIC, BOOTLOG_VERSION, sizeof(boot_report_t), out, seq, max);
}
