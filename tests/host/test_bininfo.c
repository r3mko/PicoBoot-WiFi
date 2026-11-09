/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/bininfo.c: a made-up image, and the built firmware.bin when
// PICOBOOT_BUILD_DIR is set. Built and run by tests/test_host.py.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bininfo.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static uint8_t image[16384];

// Header at 0x100, entry table at 0x200, entries at 0x300 and 0x310, the string at 0x400
static void make_image(void) {
    memset(image, 0, sizeof(image));
    put32(image + 0x100, 0x7188ebf2u);
    put32(image + 0x104, 0x10000200u);
    put32(image + 0x108, 0x10000208u);
    put32(image + 0x110, 0xe71aa390u);
    put32(image + 0x200, 0x10000300u);              // Another entry first: the program name
    put32(image + 0x204, 0x10000310u);
    put32(image + 0x300, 6u | 0x5052u << 16);
    put32(image + 0x304, 0xfdd6ba67u);
    put32(image + 0x308, 0x10000500u);
    put32(image + 0x310, 6u | 0x5052u << 16);
    put32(image + 0x314, 0x11a9bc3au);
    put32(image + 0x318, 0x10000400u);
    strcpy((char *)image + 0x400, "1.2.3+abc");
}

static void test_made_up_image(void) {
    char v[32];
    make_image();
    CHECK(bininfo_version(image, sizeof(image), v, sizeof(v)) && strcmp(v, "1.2.3+abc") == 0);
    CHECK(bininfo_version(image, sizeof(image), v, 4) && strcmp(v, "1.2") == 0);   // Cut to fit

    put32(image + 0x318, 0x20000400u);              // The string in RAM: not readable here
    CHECK(!bininfo_version(image, sizeof(image), v, sizeof(v)));
    make_image();
    put32(image + 0x318, 0x10000000u + sizeof(image));      // Past the image
    CHECK(!bininfo_version(image, sizeof(image), v, sizeof(v)));
    make_image();
    put32(image + 0x108, 0x10100000u);              // A table that runs past the image
    CHECK(!bininfo_version(image, sizeof(image), v, sizeof(v)));
    make_image();
    put32(image + 0x110, 0);                         // No end marker: no header
    CHECK(!bininfo_version(image, sizeof(image), v, sizeof(v)));

    memset(image, 0xFF, sizeof(image));             // An erased slot
    CHECK(!bininfo_version(image, sizeof(image), v, sizeof(v)));
}

static void test_built_firmware(void) {
    const char *dir = getenv("PICOBOOT_BUILD_DIR");
    if (!dir) {
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/src/firmware/firmware.bin", dir);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (!f) {
        return;
    }
    static uint8_t slot[262144];
    memset(slot, 0xFF, sizeof(slot));
    const size_t got = fread(slot, 1, sizeof(slot), f);
    fclose(f);
    CHECK(got > 0);
    char v[32];
    CHECK(bininfo_version(slot, sizeof(slot), v, sizeof(v)));
    CHECK(strncmp(v, "0.", 2) == 0 && strchr(v, '+') != NULL);     // As PICOBOOT_WIFI_BUILD
    printf("built firmware: %s\n", v);
}

int main(void) {
    test_made_up_image();
    test_built_firmware();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("bininfo: all tests passed\n");
    return 0;
}
