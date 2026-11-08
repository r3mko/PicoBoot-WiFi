/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for the Wi-Fi blob header check (src/firmware/wifiblob.c, design D14): what it lets
// through goes straight to the CYW43 driver as lengths. Built and run by tests/test_host.py.

#include <stdio.h>
#include "wifiblob.c"   // For its static valid()

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The header tools/mkwifiblob.py writes for the SDK's 7.95.49 blob
static wifiblob_header_t good(void) {
    wifiblob_header_t h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "W43B", 4);
    h.version = 1;
    h.fw_len = 224190;
    h.clm_len = 984;
    h.data_len = 224256 + 984;   // CLM after the firmware padded to 512
    return h;
}

static void test_the_real_blob_passes(void) {
    wifiblob_header_t h = good();
    CHECK(valid(&h));
}

static void test_clm_must_lie_inside_the_data(void) {
    wifiblob_header_t h = good();
    h.clm_len = h.data_len - 224256;           // Exactly to the end
    CHECK(valid(&h));
    h.clm_len++;                               // One byte past it
    CHECK(!valid(&h));

    // A CLM length that wraps the sum around (review finding, phase 1)
    h = good();
    h.fw_len = 1;
    h.clm_len = 0xFFFFFE00u;
    CHECK(!valid(&h));

    // Firmware longer than the data
    h = good();
    h.data_len = 1000;
    CHECK(!valid(&h));
}

static void test_bad_headers_fail(void) {
    wifiblob_header_t h = good();
    h.magic[3] = 'X';
    CHECK(!valid(&h));
    h = good();
    h.version = 2;
    CHECK(!valid(&h));
    h = good();
    h.fw_len = 0;
    CHECK(!valid(&h));
    h = good();
    h.fw_len = LAYOUT_WIFI_SIZE;
    CHECK(!valid(&h));
    h = good();
    h.data_len = LAYOUT_WIFI_SIZE - sizeof(h) + 1;   // More than the area holds
    CHECK(!valid(&h));
    memset(&h, 0xFF, sizeof(h));                      // Erased flash
    CHECK(!valid(&h));
}

int main(void) {
    test_the_real_blob_passes();
    test_clm_must_lie_inside_the_data();
    test_bad_headers_fail();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("wifiblob: all tests passed\n");
    return 0;
}
