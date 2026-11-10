/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/common/records.c (design D23), against a simulated NOR flash in which a
// write can be cut short, as a power cut would. Built and run by tests/test_host.py.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "records.h"
#include "bootctl.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- Simulated NOR flash: erase sets 0xFF, program can only clear bits -------------------------

static uint8_t area[RECORD_AREA_SIZE];
static long budget = -1;            // Bytes the next operations may still change; -1 = no power cut

static void fake_erase(void *ctx, uint32_t offset) {
    (void)ctx;
    if (budget == 0) {
        return;                     // The power went before the erase
    }
    memset(area + offset, 0xFF, RECORD_SECTOR_SIZE);
}

static void fake_program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    (void)ctx;
    CHECK(offset % RECORD_PAGE_SIZE == 0 && length % RECORD_PAGE_SIZE == 0);
    for (size_t i = 0; i < length; i++) {
        if (budget == 0) {
            return;                 // The power went part-way through
        }
        if (budget > 0) {
            budget--;
        }
        area[offset + i] &= data[i];
    }
}

static const record_flash_t flash = {fake_erase, fake_program, NULL};

static void reset_flash(void) {
    memset(area, 0xFF, sizeof(area));
    budget = -1;
}

static bool write_bootctl(uint8_t active_slot) {
    bootctl_t b = BOOTCTL_DEFAULT;
    b.active_slot = active_slot;
    return record_write(area, &flash, BOOTCTL_MAGIC, BOOTCTL_VERSION, &b, sizeof(b));
}

static const bootctl_t *find_bootctl(uint32_t *seq) {
    const record_header_t *rec = record_find(area, BOOTCTL_MAGIC, BOOTCTL_VERSION, sizeof(bootctl_t), seq);
    return rec ? record_payload(rec) : NULL;
}

// --- Tests ---------------------------------------------------------------------------------------

static void test_crc32_check_value(void) {
    CHECK(record_crc32(0, "123456789", 9) == 0xCBF43926u);
}

static void test_empty_area_has_no_record(void) {
    reset_flash();
    CHECK(find_bootctl(NULL) == NULL);
}

static void test_writes_alternate_between_the_sectors(void) {
    reset_flash();
    uint32_t seq;
    CHECK(write_bootctl(BOOTCTL_SLOT_B));
    CHECK(find_bootctl(&seq) && find_bootctl(NULL)->active_slot == BOOTCTL_SLOT_B && seq == 1);
    CHECK(((const record_header_t *)area)->seq == 1);

    CHECK(write_bootctl(BOOTCTL_SLOT_A));
    CHECK(find_bootctl(&seq) && find_bootctl(NULL)->active_slot == BOOTCTL_SLOT_A && seq == 2);
    CHECK(((const record_header_t *)(area + RECORD_SECTOR_SIZE))->seq == 2);
    CHECK(((const record_header_t *)area)->seq == 1);          // The older one is still there

    CHECK(write_bootctl(BOOTCTL_SLOT_B));
    CHECK(find_bootctl(&seq) && seq == 3 && ((const record_header_t *)area)->seq == 3);
}

static void test_power_cut_keeps_the_previous_record(void) {
    // Cut at every point of the write: after the erase, in the header, in the payload, at the end
    static const long cuts[] = {0, 1, 8, 15, 16, 20, (long)(sizeof(record_header_t) + sizeof(bootctl_t) - 1)};
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        reset_flash();
        CHECK(write_bootctl(BOOTCTL_SLOT_B));                 // seq 1, in sector 0
        CHECK(write_bootctl(BOOTCTL_SLOT_A));                 // seq 2, in sector 1
        budget = cuts[i];
        CHECK(!write_bootctl(BOOTCTL_SLOT_B));                // Torn write into sector 0
        budget = -1;
        uint32_t seq;
        const bootctl_t *b = find_bootctl(&seq);
        CHECK(b && b->active_slot == BOOTCTL_SLOT_A && seq == 2);
        // The next write goes to the damaged sector again and works
        CHECK(write_bootctl(BOOTCTL_SLOT_B));
        CHECK(find_bootctl(&seq) && find_bootctl(NULL)->active_slot == BOOTCTL_SLOT_B && seq == 3);
    }
}

static void test_power_cut_on_the_first_write_leaves_no_record(void) {
    reset_flash();
    budget = 10;
    CHECK(!write_bootctl(BOOTCTL_SLOT_B));
    budget = -1;
    CHECK(find_bootctl(NULL) == NULL);                        // The loader then uses BOOTCTL_DEFAULT
}

static void test_corruption_falls_back_to_the_older_record(void) {
    reset_flash();
    CHECK(write_bootctl(BOOTCTL_SLOT_B));
    CHECK(write_bootctl(BOOTCTL_SLOT_A));
    area[RECORD_SECTOR_SIZE + sizeof(record_header_t)] ^= 0x01;   // Flip a payload bit of the newer one
    const bootctl_t *b = find_bootctl(NULL);
    CHECK(b && b->active_slot == BOOTCTL_SLOT_B);
}

static void test_other_magic_version_or_length_is_ignored(void) {
    reset_flash();
    bootctl_t b = BOOTCTL_DEFAULT;
    CHECK(record_write(area, &flash, 0x12345678u, BOOTCTL_VERSION, &b, sizeof(b)));
    CHECK(find_bootctl(NULL) == NULL);
    reset_flash();
    CHECK(record_write(area, &flash, BOOTCTL_MAGIC, BOOTCTL_VERSION + 1, &b, sizeof(b)));
    CHECK(find_bootctl(NULL) == NULL);
    reset_flash();
    CHECK(record_write(area, &flash, BOOTCTL_MAGIC, BOOTCTL_VERSION, &b, sizeof(b) - 4));
    CHECK(find_bootctl(NULL) == NULL);
}

static void test_sequence_numbers_wrap_around(void) {
    reset_flash();
    CHECK(write_bootctl(BOOTCTL_SLOT_B));
    // Rewrite sector 0's record with seq 0xFFFFFFFF, as if it had been written 4 billion times
    record_header_t rec = *(const record_header_t *)area;
    bootctl_t b;
    memcpy(&b, area + sizeof(rec), sizeof(b));
    memset(area, 0xFF, RECORD_SECTOR_SIZE);
    rec.seq = 0xFFFFFFFFu;
    rec.crc = record_crc32(record_crc32(0, &rec, offsetof(record_header_t, crc)), &b, sizeof(b));
    memcpy(area, &rec, sizeof(rec));
    memcpy(area + sizeof(rec), &b, sizeof(b));

    uint32_t seq;
    CHECK(write_bootctl(BOOTCTL_SLOT_A));                     // seq 0, in sector 1
    CHECK(find_bootctl(&seq) && seq == 0 && find_bootctl(NULL)->active_slot == BOOTCTL_SLOT_A);
}

// Phase 2 review: a firmware of another record version in the other slot (D12) mustn't erase the
// newer firmware's record by writing its own twice
static void test_a_record_of_another_version_survives(void) {
    reset_flash();
    struct { uint32_t x[5]; } newer = {{1, 2, 3, 4, 5}};
    CHECK(record_write(area, &flash, BOOTCTL_MAGIC, BOOTCTL_VERSION + 1, &newer, sizeof(newer)));
    for (uint8_t i = 0; i < 4; i++) {
        CHECK(write_bootctl(i % 2 ? BOOTCTL_SLOT_A : BOOTCTL_SLOT_B));
        CHECK(find_bootctl(NULL) && find_bootctl(NULL)->active_slot == (i % 2 ? BOOTCTL_SLOT_A : BOOTCTL_SLOT_B));
        const record_header_t *other = record_find(area, BOOTCTL_MAGIC, BOOTCTL_VERSION + 1, sizeof(newer), NULL);
        CHECK(other && memcmp(record_payload(other), &newer, sizeof(newer)) == 0);
    }
    // ...and the newer firmware writing again keeps the older one's record too
    newer.x[0] = 9;
    CHECK(record_write(area, &flash, BOOTCTL_MAGIC, BOOTCTL_VERSION + 1, &newer, sizeof(newer)));
    CHECK(find_bootctl(NULL) && find_bootctl(NULL)->active_slot == BOOTCTL_SLOT_A);
}

static void test_payload_that_is_too_long_is_refused(void) {
    reset_flash();
    static uint8_t big[RECORD_SECTOR_SIZE];
    CHECK(!record_write(area, &flash, BOOTCTL_MAGIC, BOOTCTL_VERSION, big, (uint16_t)(RECORD_MAX_PAYLOAD + 1)));
}

int main(void) {
    test_crc32_check_value();
    test_empty_area_has_no_record();
    test_writes_alternate_between_the_sectors();
    test_power_cut_keeps_the_previous_record();
    test_power_cut_on_the_first_write_leaves_no_record();
    test_corruption_falls_back_to_the_older_record();
    test_other_magic_version_or_length_is_ignored();
    test_sequence_numbers_wrap_around();
    test_payload_that_is_too_long_is_refused();
    test_a_record_of_another_version_survives();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_records: all checks passed\n");
    return 0;
}
