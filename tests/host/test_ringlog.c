/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/common/ringlog.c (the boot log, design D39), against a simulated NOR flash in
// which a write can be cut short. Built and run by tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "ringlog.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define MAGIC 0x31474C42u           // "BLG1"
#define VERSION 1
#define SECTORS 2
#define ENTRIES (SECTORS * RECORD_SECTOR_SIZE / RINGLOG_ENTRY_SIZE)     // 32

typedef struct {
    uint32_t boot;
    uint8_t filler[40];
} payload_t;

static uint8_t area[SECTORS * RECORD_SECTOR_SIZE];
static long budget = -1;            // Bytes the next program may still write; -1 = no power cut

static void fake_erase(void *ctx, uint32_t offset) {
    (void)ctx;
    memset(area + offset, 0xFF, RECORD_SECTOR_SIZE);
}

static void fake_program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    (void)ctx;
    for (size_t i = 0; i < length; i++) {
        if (budget == 0) {
            return;
        }
        if (budget > 0) {
            budget--;
        }
        area[offset + i] &= data[i];
    }
}

static const record_flash_t flash = {fake_erase, fake_program, NULL};
static const ringlog_t log_ = {area, sizeof(area), &flash};

static bool append(uint32_t boot) {
    payload_t p = {.boot = boot};
    memset(p.filler, (int)boot, sizeof(p.filler));
    return ringlog_append(&log_, MAGIC, VERSION, &p, sizeof(p));
}

static unsigned read_boots(uint32_t *boots, unsigned max) {
    payload_t p[ENTRIES];
    unsigned n = ringlog_read(&log_, MAGIC, VERSION, sizeof(payload_t), p, NULL, max);
    for (unsigned i = 0; i < n; i++) {
        boots[i] = p[i].boot;
    }
    return n;
}

static void reset(uint8_t fill) {
    memset(area, fill, sizeof(area));
    budget = -1;
}

static void test_empty_log(void) {
    reset(0xFF);
    uint32_t b[4];
    CHECK(read_boots(b, 4) == 0);
}

static void test_garbage_area_is_erased_on_the_first_append(void) {
    reset(0x5A);                    // Old firmware code where the log goes (PicoBoot-DX used this flash)
    CHECK(append(1));
    uint32_t b[4];
    CHECK(read_boots(b, 4) == 1 && b[0] == 1);
}

static void test_newest_first_and_wrap_around(void) {
    reset(0xFF);
    for (uint32_t boot = 1; boot <= 100; boot++) {
        CHECK(append(boot));
    }
    uint32_t b[ENTRIES];
    unsigned n = read_boots(b, ENTRIES);
    // After a wrap the oldest sector was erased: between 17 and 32 entries remain, newest first
    CHECK(n >= ENTRIES / 2 + 1 && n <= ENTRIES);
    for (unsigned i = 0; i < n; i++) {
        CHECK(b[i] == 100 - i);
    }
}

static void test_torn_entry_keeps_the_newer_entries(void) {
    // Review finding 2: a torn page mid-sector must not erase the newer entries before it
    reset(0xFF);
    for (uint32_t boot = 1; boot <= 20; boot++) {
        CHECK(append(boot));        // Entries 0..19: sector 1 holds boots 17..20
    }
    budget = 30;
    CHECK(!append(21));             // Torn page at entry 20 (mid-sector 1)
    budget = -1;
    CHECK(append(22));              // Moves on to sector 0 (erasing boots 1..16), keeps 17..20
    uint32_t b[ENTRIES];
    unsigned n = read_boots(b, ENTRIES);
    CHECK(n == 5);
    CHECK(b[0] == 22 && b[1] == 20 && b[2] == 19 && b[3] == 18 && b[4] == 17);
    CHECK(append(23));              // And it carries on after the new entry
    n = read_boots(b, ENTRIES);
    CHECK(n == 6 && b[0] == 23 && b[1] == 22 && b[2] == 20);
}

static void test_payload_that_is_too_long_is_refused(void) {
    reset(0xFF);
    uint8_t big[RINGLOG_MAX_PAYLOAD + 1] = {0};
    CHECK(!ringlog_append(&log_, MAGIC, VERSION, big, sizeof(big)));
}

static void test_other_magic_or_version_is_ignored(void) {
    reset(0xFF);
    CHECK(append(1));
    payload_t p[1];
    CHECK(ringlog_read(&log_, MAGIC + 1, VERSION, sizeof(payload_t), p, NULL, 1) == 0);
    CHECK(ringlog_read(&log_, MAGIC, VERSION + 1, sizeof(payload_t), p, NULL, 1) == 0);
}

int main(void) {
    test_empty_log();
    test_garbage_area_is_erased_on_the_first_append();
    test_newest_first_and_wrap_around();
    test_torn_entry_keeps_the_newer_entries();
    test_payload_that_is_too_long_is_refused();
    test_other_magic_or_version_is_ignored();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_ringlog: all checks passed\n");
    return 0;
}
