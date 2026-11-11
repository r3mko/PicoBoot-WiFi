/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Feeds a file through src/firmware/payloadup.c into a simulated payload 2 slot and prints the result
// code and the payload's SHA-256: the device side reads what the Python tools and the payload
// projects' own UF2s hold. Run by tests/test_host.py.
//
//     feed_payload FILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "payloadup.h"
#include "layout.h"
#include "sha256_host.h"

static uint8_t *flash;
static sha_t io_sha;

static void erase(void *c, uint32_t o) { (void)c; memset(flash + o, 0xFF, 4096); }
static void program(void *c, uint32_t o, const uint8_t *d, size_t n) { (void)c; for (size_t i = 0; i < n; i++) flash[o + i] &= d[i]; }
static const uint8_t *read(void *c, uint32_t o) { (void)c; return flash + o; }
static void start(void *c) { (void)c; sha_start(&io_sha); }
static void update(void *c, const uint8_t *d, size_t n) { (void)c; sha_update(&io_sha, d, n); }
static void finish(void *c, uint8_t out[32]) { (void)c; sha_finish(&io_sha, out); }
static bool emptied(void *c) { (void)c; return true; }

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: feed_payload FILE\n");
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    static uint8_t data[8 << 20];
    const size_t size = fread(data, 1, sizeof(data), f);
    fclose(f);
    flash = malloc(LAYOUT_FLASH_SIZE);
    memset(flash, 0xFF, LAYOUT_FLASH_SIZE);
    static const payloadup_io_t io = {erase, program, read, start, update, finish, emptied, NULL};
    static payloadup_t u;
    payloadup_begin(&u, &io, LAYOUT_PAYLOAD2_OFFSET, LAYOUT_PAYLOAD2_SIZE, (uint32_t)size);
    for (size_t at = 0; at < size; at += 1460) {
        payloadup_feed(&u, data + at, size - at < 1460 ? size - at : 1460);
    }
    const payloadup_result_t r = payloadup_finish(&u);
    printf("%s ", payloadup_result_code(r));
    for (int i = 0; i < 32; i++) printf("%02x", r == PAYLOADUP_OK ? u.sha256[i] : 0);
    printf(" %lu\n", (unsigned long)(r == PAYLOADUP_OK ? u.payload_size : 0));
    free(flash);
    return r == PAYLOADUP_OK ? 0 : 1;
}
