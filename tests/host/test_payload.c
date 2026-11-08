/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for the payload check (src/firmware/payload.c), which decides how much the injection's
// DMA reads. Built and run by tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "payload.h"
#include "endian.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define REGION_WORDS 1024u                 // A 4 KB region
static uint32_t region[REGION_WORDS + 16]; // Room past the region, to see nothing reads there

// A payload of `size` bytes: header, filler, and the footer where the size says
static void make(uint32_t size) {
    memset(region, 0xA5, sizeof(region));
    region[0] = BigEndian32(0x49504C42);   // "IPLB"
    region[1] = BigEndian32(0x4F4F5420);   // "OOT "
    region[2] = BigEndian32(size);
    if (size >= 4 && size / 4 <= REGION_WORDS) {
        region[size / 4 - 1] = BigEndian32(0x5049434F);   // "PICO"
    }
}

static size_t check(void) {
    return payload_check(region, REGION_WORDS * 4);
}

static void test_valid_payloads_are_rounded_up_to_whole_transfers(void) {
    make(36);                              // The smallest: header and footer only
    CHECK(check() == 256);
    make(1024);
    CHECK(check() == 256);
    make(1028);
    CHECK(check() == 512);
    make(REGION_WORDS * 4);                // Exactly fills the region
    CHECK(check() == REGION_WORDS);
}

static void test_sizes_that_dont_fit_are_refused(void) {
    make(35);                              // Smaller than header and footer
    CHECK(check() == PAYLOAD_INVALID);
    make(REGION_WORDS * 4 + 4);            // Larger than the region
    CHECK(check() == PAYLOAD_INVALID);
    make(0xFFFFFFFCu);                     // A size that would wrap
    CHECK(check() == PAYLOAD_INVALID);

    // Fits, but its last 1K transfer would read past the region
    make(1028);
    CHECK(payload_check(region, 1100) == PAYLOAD_INVALID);
}

static void test_damaged_payloads_are_refused(void) {
    make(1024);
    region[255] = 0;                       // No footer
    CHECK(check() == PAYLOAD_INVALID);
    make(1024);
    region[0] = 0;                         // No "IPLB"
    CHECK(check() == PAYLOAD_INVALID);
    make(1024);
    region[1] = 0;                         // No "OOT "
    CHECK(check() == PAYLOAD_INVALID);
    memset(region, 0xFF, sizeof(region));  // Erased flash
    CHECK(check() == PAYLOAD_INVALID);
    make(36);
    CHECK(payload_check(region, 8) == PAYLOAD_INVALID);   // A region too small for the header
}

int main(void) {
    test_valid_payloads_are_rounded_up_to_whole_transfers();
    test_sizes_that_dont_fit_are_refused();
    test_damaged_payloads_are_refused();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("payload: all tests passed\n");
    return 0;
}
