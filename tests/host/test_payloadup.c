/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/payloadup.c (design D15, D16, D42): raw payloads and payload UF2s of one
// or two families streamed into a simulated payload slot, in odd chunk sizes, with every refusal; and
// payload_fingerprint() (D17). Files made by the Python tools are tested by tests/test_host.py.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "payloadup.h"
#include "payload.h"
#include "layout.h"
#include "sha256_host.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define SLOT_OFFSET LAYOUT_PAYLOAD2_OFFSET
#define SLOT_SIZE   LAYOUT_PAYLOAD2_SIZE

static uint8_t *flash;
static int erases, emptied_calls, erases_before_emptied;
static bool emptied_fails;
static sha_t io_sha;

static void fake_erase(void *c, uint32_t o) {
    (void)c;
    CHECK(o % 4096 == 0 && o >= SLOT_OFFSET && o + 4096 <= SLOT_OFFSET + SLOT_SIZE);   // Only the slot
    if (!emptied_calls) erases_before_emptied++;
    memset(flash + o, 0xFF, 4096);
    erases++;
}
static void fake_program(void *c, uint32_t o, const uint8_t *d, size_t n) {
    (void)c;
    CHECK(o % 256 == 0 && n % 256 == 0 && o >= SLOT_OFFSET && o + n <= SLOT_OFFSET + SLOT_SIZE);
    for (size_t i = 0; i < n; i++) flash[o + i] &= d[i];
}
static const uint8_t *fake_read(void *c, uint32_t o) { (void)c; return flash + o; }
static void io_start(void *c) { (void)c; sha_start(&io_sha); }
static void io_update(void *c, const uint8_t *d, size_t n) { (void)c; sha_update(&io_sha, d, n); }
static void io_finish(void *c, uint8_t out[32]) { (void)c; sha_finish(&io_sha, out); }
static bool fake_emptied(void *c) { (void)c; emptied_calls++; return !emptied_fails; }

static const payloadup_io_t io = {fake_erase, fake_program, fake_read, io_start, io_update, io_finish, fake_emptied, NULL};

static void reset_flash(void) {
    memset(flash, 0xA5, LAYOUT_FLASH_SIZE);
    erases = emptied_calls = erases_before_emptied = 0;
    emptied_fails = false;
}

// --- Payloads and UF2s ------------------------------------------------------------------------------

static uint8_t payload[100004];         // A valid payload of this many bytes

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void make_payload(void) {
    memset(payload, 0, sizeof(payload));
    memcpy(payload, "IPLBOOT ", 8);
    put_be32(payload + 8, sizeof(payload));
    for (size_t i = 32; i < sizeof(payload) - 4; i++) payload[i] = (uint8_t)(i * 31 + (i >> 8));
    memcpy(payload + sizeof(payload) - 4, "PICO", 4);
}

typedef struct { uint8_t *data; size_t size; } file_t;

// One family's blocks of 256 bytes from `base`, padded with 0xFF as payload UF2s are
static size_t uf2_family(uint8_t *out, const uint8_t *data, size_t n, uint32_t base, uint32_t family) {
    const size_t blocks = (n + 255) / 256;
    for (size_t i = 0; i < blocks; i++) {
        uint8_t *b = out + i * 512;
        memset(b, 0, 512);
        put32(b, 0x0A324655u);
        put32(b + 4, 0x9E5D5157u);
        put32(b + 8, 0x2000u);
        put32(b + 12, base + (uint32_t)i * 256);
        put32(b + 16, 256);
        put32(b + 20, (uint32_t)i);
        put32(b + 24, (uint32_t)blocks);
        put32(b + 28, family);
        memset(b + 32, 0xFF, 256);
        memcpy(b + 32, data + i * 256, n - i * 256 < 256 ? n - i * 256 : 256);
        put32(b + 508, 0x0AB16F30u);
    }
    return blocks * 512;
}

static file_t make_uf2(int families) {
    file_t f;
    const size_t one = (sizeof(payload) + 255) / 256 * 512;
    f.data = malloc(one * 2);
    f.size = uf2_family(f.data, payload, sizeof(payload), PAYLOADUP_UF2_BASE, 0xe48bff56u);     // rp2040
    if (families == 2) {
        f.size += uf2_family(f.data + f.size, payload, sizeof(payload), PAYLOADUP_UF2_BASE, 0xe48bff59u);
    }
    return f;
}

static payloadup_result_t upload(const uint8_t *data, size_t size, size_t chunk, size_t stop_after, payloadup_t *u) {
    payloadup_begin(u, &io, SLOT_OFFSET, SLOT_SIZE, (uint32_t)size);
    size_t at = 0, odd = 1;
    const size_t end = stop_after ? stop_after : size;
    while (at < end) {
        size_t n = chunk ? chunk : (odd = odd * 7 % 1531 + 1);
        if (n > end - at) n = end - at;
        if (payloadup_feed(u, data + at, n) != PAYLOADUP_OK) break;
        at += n;
    }
    return payloadup_finish(u);
}

static bool slot_holds_payload(void) {
    return memcmp(flash + SLOT_OFFSET, payload, sizeof(payload)) == 0;
}

static bool slot_first_sector_erased(void) {
    for (int i = 0; i < 4096; i++) if (flash[SLOT_OFFSET + i] != 0xFF) return false;
    return true;
}

// --- Tests -----------------------------------------------------------------------------------------

static void test_raw_and_uf2s(void) {
    static payloadup_t u;
    uint8_t want[32];
    sha(payload, sizeof(payload), want);
    const size_t chunks[] = {0, 1, 511, 512, 1460, 4096, 1000000};
    for (unsigned c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
        reset_flash();
        CHECK(upload(payload, sizeof(payload), chunks[c], 0, &u) == PAYLOADUP_OK);
        CHECK(slot_holds_payload() && u.payload_size == sizeof(payload) && memcmp(u.sha256, want, 32) == 0);
        CHECK(emptied_calls == 1 && erases_before_emptied == 0);
        for (int fam = 1; fam <= 2; fam++) {           // gekkoboot's rp2040 only, Swiss's universal
            file_t f = make_uf2(fam);
            reset_flash();
            CHECK(upload(f.data, f.size, chunks[c], 0, &u) == PAYLOADUP_OK);
            CHECK(slot_holds_payload() && memcmp(u.sha256, want, 32) == 0);
            free(f.data);
        }
    }
}

static void test_refusals_before_any_erase(void) {
    static payloadup_t u;
    static uint8_t junk[2048];
    reset_flash();
    memset(junk, 0x55, sizeof(junk));
    CHECK(upload(junk, sizeof(junk), 0, 0, &u) == PAYLOADUP_NOT_A_PAYLOAD && erases == 0);
    memcpy(junk, "IPLBOOX ", 8);                         // "IPLB" but not "IPLBOOT "
    CHECK(upload(junk, sizeof(junk), 0, 0, &u) == PAYLOADUP_NOT_A_PAYLOAD && erases == 0);
    memcpy(junk, "IPLBOOT ", 8);
    put_be32(junk + 8, SLOT_SIZE + 4);                   // Bigger than the slot
    CHECK(upload(junk, sizeof(junk), 0, 0, &u) == PAYLOADUP_TOO_LARGE && erases == 0);
    put_be32(junk + 8, 1001);                             // Not whole words
    CHECK(upload(junk, sizeof(junk), 0, 0, &u) == PAYLOADUP_BAD_PAYLOAD && erases == 0);
    CHECK(upload(junk, 5, 0, 0, &u) == PAYLOADUP_NOT_A_PAYLOAD);           // Too short to tell

    // A UF2 without blocks at 0x10080000: the USB install file, or a firmware UF2
    static uint8_t other[4 * 512];
    const size_t n = uf2_family(other, payload, 1024, 0x10000000u, 0xe48bff59u);
    reset_flash();
    CHECK(upload(other, n, 0, 0, &u) == PAYLOADUP_NOT_A_PAYLOAD && erases == 0);
}

static void test_bad_uf2s(void) {
    static payloadup_t u;
    file_t f = make_uf2(1);
    // A missing block: the next one is out of order. Within the first sector nothing is erased yet;
    // further on, the slot is left without its first sector
    file_t g = {malloc(f.size), f.size - 512};
    memcpy(g.data, f.data, 512 * 10);
    memcpy(g.data + 512 * 10, f.data + 512 * 11, f.size - 512 * 11);
    reset_flash();
    CHECK(upload(g.data, g.size, 0, 0, &u) == PAYLOADUP_BAD_UF2 && erases == 0);
    memcpy(g.data, f.data, 512 * 100);
    memcpy(g.data + 512 * 100, f.data + 512 * 101, f.size - 512 * 101);
    reset_flash();
    CHECK(upload(g.data, g.size, 0, 0, &u) == PAYLOADUP_BAD_UF2 && slot_first_sector_erased());
    // A broken block magic
    memcpy(g.data, f.data, f.size);
    g.size = f.size;
    g.data[512 * 5 + 4] ^= 1;
    reset_flash();
    CHECK(upload(g.data, g.size, 0, 0, &u) == PAYLOADUP_BAD_UF2);
    // Not whole blocks
    reset_flash();
    CHECK(upload(f.data, f.size - 100, 0, 0, &u) == PAYLOADUP_BAD_UF2 || u.result == PAYLOADUP_BAD_PAYLOAD);
    // Blocks before the payload's start, of another family, or flagged not-main-flash, are skipped
    static uint8_t mixed[2 * 512 + sizeof(payload) * 2 + 2048];
    size_t m = uf2_family(mixed, payload, 512, 0x10000000u, 0xe48bff59u);
    put32(mixed + 512 + 8, 0x2001u);                      // ...this one not-main-flash
    m += uf2_family(mixed + m, payload, sizeof(payload), PAYLOADUP_UF2_BASE, 0xe48bff57u);
    reset_flash();
    CHECK(upload(mixed, m, 0, 0, &u) == PAYLOADUP_OK && slot_holds_payload());
    free(f.data);
    free(g.data);
}

static void test_bad_payloads(void) {
    static payloadup_t u;
    reset_flash();                                       // The data stops before the size
    CHECK(upload(payload, sizeof(payload) - 400, 0, 0, &u) == PAYLOADUP_BAD_PAYLOAD && slot_first_sector_erased());
    payload[sizeof(payload) - 1] = 'X';                  // No footer
    reset_flash();
    CHECK(upload(payload, sizeof(payload), 0, 0, &u) == PAYLOADUP_BAD_PAYLOAD && slot_first_sector_erased());
    make_payload();
    reset_flash();                                       // Cut off
    payloadup_begin(&u, &io, SLOT_OFFSET, SLOT_SIZE, sizeof(payload));
    CHECK(payloadup_feed(&u, payload, 50000) == PAYLOADUP_OK);
    CHECK(payloadup_finish(&u) == PAYLOADUP_INCOMPLETE && slot_first_sector_erased());
    reset_flash();                                       // Boot control can't be written
    emptied_fails = true;
    CHECK(upload(payload, sizeof(payload), 0, 0, &u) == PAYLOADUP_FLASH_WRITE && erases == 0);
    // More than Content-Length
    reset_flash();
    payloadup_begin(&u, &io, SLOT_OFFSET, SLOT_SIZE, 1000);
    CHECK(payloadup_feed(&u, payload, 1001) == PAYLOADUP_BAD_PAYLOAD);
}

static void test_fingerprint(void) {
    static uint32_t region[2048];
    memset(region, 0xFF, sizeof(region));
    CHECK(payload_fingerprint(region, sizeof(region)) == 0);        // No payload
    memcpy(region, payload, 4096);
    memcpy((uint8_t *)region + 4096 - 4, "PICO", 4);
    put_be32((uint8_t *)region + 8, 4096);
    const uint32_t a = payload_fingerprint(region, sizeof(region));
    CHECK(a != 0);
    ((uint8_t *)region)[100] ^= 1;                                   // In the first 256 bytes
    CHECK(payload_fingerprint(region, sizeof(region)) != a);
    ((uint8_t *)region)[100] ^= 1;
    ((uint8_t *)region)[2000] ^= 1;                                  // Elsewhere: not covered (D17)
    CHECK(payload_fingerprint(region, sizeof(region)) == a);
}

static void test_codes(void) {
    CHECK(strcmp(payloadup_result_code(PAYLOADUP_OK), "ok") == 0);
    CHECK(strcmp(payloadup_result_code(PAYLOADUP_BAD_UF2), "bad_uf2") == 0);
}

int main(void) {
    flash = malloc(LAYOUT_FLASH_SIZE);
    make_payload();
    test_raw_and_uf2s();
    test_refusals_before_any_erase();
    test_bad_uf2s();
    test_bad_payloads();
    test_fingerprint();
    test_codes();
    free(flash);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("payloadup: all tests passed\n");
    return 0;
}
