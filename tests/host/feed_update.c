/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Feeds an update file through src/firmware/update.c into a simulated flash and prints the result
// code: proves the device side reads what tools/mkupdate.py writes, and that the header it rebuilds
// for a firmware backup (update_build_header(), D42) is the one mkupdate.py wrote. Run by
// tests/test_host.py.
//
//     feed_update FILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update.h"
#include "bootctl.h"
#include "layout.h"
#include "bootinfo.h"
#include "sha256_host.h"

static uint8_t *flash;
static sha_t io_sha;

static void erase(void *c, uint32_t o) { (void)c; memset(flash + o, 0xFF, LAYOUT_SECTOR_SIZE); }
static void program(void *c, uint32_t o, const uint8_t *d, size_t n) { (void)c; for (size_t i = 0; i < n; i++) flash[o + i] &= d[i]; }
static const uint8_t *read(void *c, uint32_t o) { (void)c; return flash + o; }
static void start(void *c) { (void)c; sha_start(&io_sha); }
static void update(void *c, const uint8_t *d, size_t n) { (void)c; sha_update(&io_sha, d, n); }
static void finish(void *c, uint8_t out[32]) { (void)c; sha_finish(&io_sha, out); }
static bool emptied(void *c, uint8_t s) { (void)c; (void)s; return true; }

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: feed_update FILE\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    static uint8_t data[1 << 20];
    const size_t size = fread(data, 1, sizeof(data), f);
    fclose(f);
    flash = malloc(LAYOUT_FLASH_SIZE);
    memset(flash, 0xFF, LAYOUT_FLASH_SIZE);

    // The firmware of this source tree, on the oldest loader it works with
    static const update_io_t io = {erase, program, read, start, update, finish, emptied, NULL};
    const update_env_t env = {"pico2_w", LAYOUT_VERSION, BOOTINFO_LOADER_NEEDED};
    static update_t u;
    update_begin(&u, &io, &env, BOOTCTL_SLOT_B, (uint32_t)size);
    for (size_t at = 0; at < size; at += 1460) {        // In TCP-segment-sized pieces
        update_feed(&u, data + at, size - at < 1460 ? size - at : 1460);
    }
    update_result_t r = update_finish(&u);
    if (r == UPDATE_OK && memcmp(flash + LAYOUT_SLOT_B_OFFSET, data + UPDATE_HEADER_SIZE, u.header.image_size) != 0) {
        r = UPDATE_BAD_IMAGE;
    }
    if (r == UPDATE_OK) {
        uint8_t again[UPDATE_HEADER_SIZE];
        update_build_header(&u.header, again);
        sha_start(&io_sha);
        sha_update(&io_sha, again, UPDATE_HEADER_HASHED);
        sha_finish(&io_sha, again + UPDATE_HEADER_HASHED);
        if (memcmp(again, data, UPDATE_HEADER_SIZE) != 0) {
            printf("rebuilt_header_differs\n");
            free(flash);
            return 1;
        }
    }
    printf("%s %s\n", update_result_code(r), u.header.version);
    free(flash);
    return r == UPDATE_OK ? 0 : 1;
}
