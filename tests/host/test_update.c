/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/update.c (design D11, D14, docs/update-format.md): whole uploads into
// a simulated 4 MB NOR flash, fed in odd chunk sizes, and every refusal. Built and run by
// tests/test_host.py.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update.h"
#include "bootctl.h"
#include "layout.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- SHA-256 (FIPS 180-4), for the tests only: the firmware uses the RP2350's hardware -------------

typedef struct { uint32_t h[8]; uint8_t buf[64]; size_t fill; uint64_t bits; } sha_t;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(sha_t *s, const uint8_t *p) {
    uint32_t w[64], a[8];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(a, s->h, sizeof(a));
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = a[7] + (ROR(a[4], 6) ^ ROR(a[4], 11) ^ ROR(a[4], 25)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + K[i] + w[i];
        uint32_t t2 = (ROR(a[0], 2) ^ ROR(a[0], 13) ^ ROR(a[0], 22)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        memmove(a + 1, a, 7 * sizeof(uint32_t));
        a[4] += t1;
        a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) s->h[i] += a[i];
}

static void sha_start(sha_t *s) {
    static const uint32_t h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->h, h0, sizeof(h0));
    s->fill = 0;
    s->bits = 0;
}

static void sha_update(sha_t *s, const uint8_t *d, size_t n) {
    s->bits += (uint64_t)n * 8;
    while (n--) {
        s->buf[s->fill++] = *d++;
        if (s->fill == 64) { sha_block(s, s->buf); s->fill = 0; }
    }
}

static void sha_finish(sha_t *s, uint8_t out[32]) {
    uint64_t bits = s->bits;
    uint8_t pad = 0x80, zero = 0, len[8];
    sha_update(s, &pad, 1);
    while (s->fill != 56) sha_update(s, &zero, 1);
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha_update(s, len, 8);
    for (int i = 0; i < 8; i++) { out[4 * i] = s->h[i] >> 24; out[4 * i + 1] = s->h[i] >> 16; out[4 * i + 2] = s->h[i] >> 8; out[4 * i + 3] = s->h[i]; }
}

static void sha(const uint8_t *d, size_t n, uint8_t out[32]) {
    sha_t s;
    sha_start(&s);
    sha_update(&s, d, n);
    sha_finish(&s, out);
}

// --- Simulated flash and the io callbacks ----------------------------------------------------------

static uint8_t *flash;              // LAYOUT_FLASH_SIZE bytes
static int erases, programs, emptied_calls, erases_before_emptied;
static bool emptied_fails;
static long flip_bit_at = -1;       // Program this flash offset with one bit wrong (a bad write)
static sha_t io_sha;

static void fake_erase(void *ctx, uint32_t offset) {
    (void)ctx;
    CHECK(offset % LAYOUT_SECTOR_SIZE == 0 && offset + LAYOUT_SECTOR_SIZE <= LAYOUT_FLASH_SIZE);
    if (!emptied_calls) erases_before_emptied++;
    memset(flash + offset, 0xFF, LAYOUT_SECTOR_SIZE);
    erases++;
}

static void fake_program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    (void)ctx;
    CHECK(offset % 256 == 0 && length % 256 == 0 && offset + length <= LAYOUT_FLASH_SIZE);
    for (size_t i = 0; i < length; i++) {
        uint8_t b = data[i];
        if ((long)(offset + i) == flip_bit_at) b ^= 0x10;
        flash[offset + i] &= b;
    }
    programs++;
}

static const uint8_t *fake_read(void *ctx, uint32_t offset) { (void)ctx; return flash + offset; }
static void io_start(void *ctx) { (void)ctx; sha_start(&io_sha); }
static void io_update(void *ctx, const uint8_t *d, size_t n) { (void)ctx; sha_update(&io_sha, d, n); }
static void io_finish(void *ctx, uint8_t out[32]) { (void)ctx; sha_finish(&io_sha, out); }
static bool fake_emptied(void *ctx, uint8_t slot) { (void)ctx; CHECK(slot == BOOTCTL_SLOT_B); emptied_calls++; return !emptied_fails; }

static const update_io_t io = {fake_erase, fake_program, fake_read, io_start, io_update, io_finish, fake_emptied, NULL};
static const update_env_t env = {"pico2_w", LAYOUT_VERSION, 2};

static void reset_flash(void) {
    memset(flash, 0xA5, LAYOUT_FLASH_SIZE);     // Old contents everywhere
    erases = programs = emptied_calls = erases_before_emptied = 0;
    emptied_fails = false;
    flip_bit_at = -1;
}

// --- Update files ----------------------------------------------------------------------------------

typedef struct {
    uint8_t *data;
    size_t size;
} file_t;

static uint8_t image[150000], blob[225304];

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void make_parts(void) {
    for (size_t i = 0; i < sizeof(image); i++) image[i] = (uint8_t)(i * 7 + (i >> 9));
    memset(blob, 0, sizeof(blob));
    memcpy(blob, "W43B", 4);
    put32(blob + 4, 1);
    put32(blob + 16, sizeof(blob) - 64);
    for (size_t i = 64; i < sizeof(blob); i++) blob[i] = (uint8_t)(i * 13);
}

// Makes a file as tools/mkupdate.py would; edit(header) may change the header before its hash
static file_t make_file(bool with_blob, void (*edit)(uint8_t *h)) {
    file_t f;
    f.size = 256 + sizeof(image) + (with_blob ? sizeof(blob) : 0);
    f.data = calloc(1, f.size);
    uint8_t *h = f.data;
    memcpy(h, "PBWU", 4);
    h[4] = 1; h[6] = 0; h[7] = 1;               // format 1, header size 256
    put32(h + 8, with_blob ? UPDATE_FLAG_BLOB : 0);
    put32(h + 12, LAYOUT_VERSION);
    put32(h + 16, 2);
    put32(h + 20, sizeof(image));
    put32(h + 24, with_blob ? sizeof(blob) : 0);
    strcpy((char *)h + 32, "pico2_w");
    strcpy((char *)h + 48, "0.3.0-dev+1a2b3c4");
    sha(image, sizeof(image), h + 80);
    sha(blob, sizeof(blob), h + 112);
    if (edit) edit(h);
    sha(h, 224, h + 224);
    memcpy(f.data + 256, image, sizeof(image));
    if (with_blob) memcpy(f.data + 256 + sizeof(image), blob, sizeof(blob));
    return f;
}

// Feeds a file in chunks of `chunk` bytes (0: odd changing sizes) and finishes
static update_result_t upload(const file_t *f, size_t chunk, size_t stop_after) {
    static update_t u;
    update_begin(&u, &io, &env, BOOTCTL_SLOT_B, (uint32_t)f->size);
    size_t at = 0, odd = 1;
    const size_t end = stop_after ? stop_after : f->size;
    while (at < end) {
        size_t n = chunk ? chunk : (odd = odd * 7 % 1531 + 1);
        if (n > end - at) n = end - at;
        if (update_feed(&u, f->data + at, n) != UPDATE_OK) break;
        at += n;
    }
    return update_finish(&u);
}

static bool slot_b_holds_image(void) {
    return memcmp(flash + LAYOUT_SLOT_B_OFFSET, image, sizeof(image)) == 0;
}

static bool wifi_b_holds_blob(void) {
    return memcmp(flash + LAYOUT_WIFI_B_OFFSET, blob, sizeof(blob)) == 0;
}

static bool untouched(uint32_t offset, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) if (flash[offset + i] != 0xA5) return false;
    return true;
}

// --- Tests -----------------------------------------------------------------------------------------

static void test_whole_update(void) {
    const size_t chunks[] = {0, 1, 255, 1460, 4096, 1000000};
    file_t f = make_file(true, NULL);
    for (unsigned i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        reset_flash();
        CHECK(upload(&f, chunks[i], 0) == UPDATE_OK);
        CHECK(slot_b_holds_image() && wifi_b_holds_blob());
        CHECK(emptied_calls == 1 && erases_before_emptied == 0);       // Boot control first (D12)
        CHECK(untouched(LAYOUT_SLOT_A_OFFSET, LAYOUT_SLOT_SIZE));      // The running slot...
        CHECK(untouched(LAYOUT_WIFI_A_OFFSET, LAYOUT_WIFI_SIZE));      // ...and its Wi-Fi area
        CHECK(untouched(LAYOUT_BOOTCTL_OFFSET, LAYOUT_BOOTCTL_SIZE));
        // 37 sectors of image, 56 of blob: each erased once
        CHECK(erases == (int)((sizeof(image) + 4095) / 4096 + (sizeof(blob) + 4095) / 4096));
    }
    free(f.data);
}

static void test_blob_written_only_when_the_area_differs(void) {
    file_t f = make_file(true, NULL);
    reset_flash();
    memcpy(flash + LAYOUT_WIFI_B_OFFSET, blob, sizeof(blob));         // Wi-Fi B already has it
    CHECK(upload(&f, 0, 0) == UPDATE_OK);
    CHECK(slot_b_holds_image() && wifi_b_holds_blob());
    CHECK(erases == (int)((sizeof(image) + 4095) / 4096));              // Only the image's sectors
    free(f.data);
}

static void test_file_without_blob(void) {
    file_t f = make_file(false, NULL);
    reset_flash();
    memcpy(flash + LAYOUT_WIFI_B_OFFSET, blob, sizeof(blob));
    CHECK(upload(&f, 0, 0) == UPDATE_OK && slot_b_holds_image());

    reset_flash();                                                      // Wi-Fi B holds another one
    CHECK(upload(&f, 0, 0) == UPDATE_NEEDS_BLOB);
    CHECK(erases == 0 && emptied_calls == 0);
    free(f.data);
}

static void e_magic(uint8_t *h) { h[0] = 'X'; }
static void e_format(uint8_t *h) { h[4] = 2; }
static void e_header_size(uint8_t *h) { h[6] = 0; h[7] = 2; }
static void e_board(uint8_t *h) { strcpy((char *)h + 32, "pico_w"); }
static void e_board_unterminated(uint8_t *h) { memset(h + 32, 'b', 16); }
static void e_layout(uint8_t *h) { put32(h + 12, LAYOUT_VERSION + 1); }
static void e_loader_newer(uint8_t *h) { put32(h + 16, 3); }
static void e_loader_older(uint8_t *h) { put32(h + 16, 1); }
static void e_flags(uint8_t *h) { put32(h + 8, UPDATE_FLAG_BLOB | 2); }
static void e_image_zero(uint8_t *h) { put32(h + 20, 0); }
static void e_image_big(uint8_t *h) { put32(h + 20, LAYOUT_SLOT_SIZE + 1); }
static void e_blob_big(uint8_t *h) { put32(h + 24, LAYOUT_WIFI_SIZE + 1); }
static void e_reserved1(uint8_t *h) { h[28] = 1; }
static void e_reserved2(uint8_t *h) { h[200] = 1; }
static void e_version_text(uint8_t *h) { h[48] = 0x07; }
static void e_version_after_nul(uint8_t *h) { h[70] = 'x'; }

static void test_header_refusals(void) {
    static const struct { void (*edit)(uint8_t *); update_result_t want; } cases[] = {
        {e_magic, UPDATE_NOT_AN_UPDATE},
        {e_format, UPDATE_UNSUPPORTED_FORMAT},
        {e_header_size, UPDATE_UNSUPPORTED_FORMAT},
        {e_board, UPDATE_WRONG_BOARD},
        {e_board_unterminated, UPDATE_BAD_HEADER},
        {e_layout, UPDATE_NEEDS_USB_INSTALL},
        {e_loader_newer, UPDATE_NEEDS_USB_INSTALL},
        {e_flags, UPDATE_BAD_HEADER},
        {e_image_zero, UPDATE_BAD_HEADER},
        {e_image_big, UPDATE_BAD_HEADER},
        {e_blob_big, UPDATE_BAD_HEADER},
        {e_reserved1, UPDATE_BAD_HEADER},
        {e_reserved2, UPDATE_BAD_HEADER},
        {e_version_text, UPDATE_BAD_HEADER},
        {e_version_after_nul, UPDATE_BAD_HEADER},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        file_t f = make_file(true, cases[i].edit);
        reset_flash();
        const update_result_t r = upload(&f, 0, 0);
        if (r != cases[i].want) printf("  case %u: got %s\n", i, update_result_code(r));
        CHECK(r == cases[i].want);
        CHECK(erases == 0 && programs == 0 && emptied_calls == 0);     // Nothing written
        free(f.data);
    }

    // An older loader requirement is fine; the header hash itself; a UF2 file; the length
    file_t f = make_file(true, e_loader_older);
    reset_flash();
    CHECK(upload(&f, 0, 0) == UPDATE_OK);
    free(f.data);

    f = make_file(true, NULL);
    f.data[100] ^= 1;                                                   // After the hash was made
    reset_flash();
    CHECK(upload(&f, 0, 0) == UPDATE_BAD_HEADER && erases == 0);
    memcpy(f.data, "UF2\n", 4);
    CHECK(upload(&f, 0, 0) == UPDATE_USB_INSTALL_FILE && erases == 0);
    free(f.data);

    f = make_file(true, NULL);
    static update_t u;
    update_begin(&u, &io, &env, BOOTCTL_SLOT_B, (uint32_t)f.size - 1);  // Content-Length one short
    CHECK(update_feed(&u, f.data, f.size - 1) == UPDATE_BAD_SIZE);
    update_begin(&u, &io, &env, BOOTCTL_SLOT_B, 100);                   // Shorter than a header
    CHECK(update_feed(&u, f.data, 100) == UPDATE_BAD_SIZE);
    free(f.data);
}

static void test_bad_parts(void) {
    file_t f = make_file(true, NULL);
    f.data[256 + 5000] ^= 0x40;                                         // A byte of the image
    reset_flash();
    CHECK(upload(&f, 0, 0) == UPDATE_BAD_IMAGE);
    CHECK(emptied_calls == 1 && untouched(LAYOUT_SLOT_A_OFFSET, LAYOUT_SLOT_SIZE));
    f.data[256 + 5000] ^= 0x40;
    f.data[256 + sizeof(image) + 100000] ^= 0x01;                       // A byte of the blob
    reset_flash();
    CHECK(upload(&f, 0, 0) == UPDATE_BAD_BLOB);
    f.data[256 + sizeof(image) + 100000] ^= 0x01;

    reset_flash();                                                      // Cut off in the image
    CHECK(upload(&f, 0, 256 + 70000) == UPDATE_INCOMPLETE);
    reset_flash();                                                      // ...in the header
    CHECK(upload(&f, 0, 100) == UPDATE_INCOMPLETE && erases == 0);

    reset_flash();                                                      // A bad flash write
    flip_bit_at = LAYOUT_SLOT_B_OFFSET + 9000;
    CHECK(upload(&f, 0, 0) == UPDATE_BAD_IMAGE);
    reset_flash();
    flip_bit_at = LAYOUT_WIFI_B_OFFSET + 200000;
    CHECK(upload(&f, 0, 0) == UPDATE_BAD_BLOB);

    reset_flash();                                                      // Boot control can't be written
    emptied_fails = true;
    CHECK(upload(&f, 0, 0) == UPDATE_FLASH_WRITE && erases == 0);

    // More bytes than the header announced (the caller's Content-Length is wrong)
    reset_flash();
    static update_t u;
    update_begin(&u, &io, &env, BOOTCTL_SLOT_B, (uint32_t)f.size);
    CHECK(update_feed(&u, f.data, f.size) == UPDATE_OK);
    CHECK(update_feed(&u, f.data, 1) == UPDATE_BAD_SIZE);
    free(f.data);
}

static void test_codes(void) {
    CHECK(strcmp(update_result_code(UPDATE_OK), "ok") == 0);
    CHECK(strcmp(update_result_code(UPDATE_USB_INSTALL_FILE), "not_an_update") == 0);
    CHECK(strcmp(update_result_code(UPDATE_NEEDS_USB_INSTALL), "needs_usb_install") == 0);
    CHECK(strcmp(update_result_code(UPDATE_FLASH_WRITE), "flash_write") == 0);
}

int main(void) {
    // A known SHA-256 vector first, so a broken test hash can't hide anything
    uint8_t d[32];
    sha((const uint8_t *)"abc", 3, d);
    CHECK(d[0] == 0xba && d[1] == 0x78 && d[30] == 0x15 && d[31] == 0xad);

    flash = malloc(LAYOUT_FLASH_SIZE);
    make_parts();
    test_whole_update();
    test_blob_written_only_when_the_area_differs();
    test_file_without_blob();
    test_header_refusals();
    test_bad_parts();
    test_codes();
    free(flash);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("update: all tests passed\n");
    return 0;
}
