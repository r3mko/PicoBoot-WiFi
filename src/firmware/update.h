/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Over-the-air firmware updates (design D11, D14, docs/update-format.md): the checks of the update
// file, and writing it into the slot that isn't running as it streams in. Flash, SHA-256 and boot
// control come in through update_io_t, so this is plain C for host tests.

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_HEADER_SIZE      256u
#define UPDATE_FORMAT           1u
#define UPDATE_FLAG_BLOB        (1u << 0)       // The Wi-Fi blob is included
#define UPDATE_BOARD_MAX        15u             // Characters; the field is 16 bytes, zero-padded
#define UPDATE_VERSION_MAX      31u             // Characters; the field is 32 bytes, zero-padded
#define UPDATE_HEADER_HASHED    224u            // The header's SHA-256 covers bytes 0 to 223, and follows them
// The oldest firmware an update may bring, by its loader requirement: loader 3 came with boot control
// format 2 (D42), and a firmware built for an older loader reads only format 1, so it would run on
// the defaults (phase 4 review). Later formats only add fields, so this stays 3.
#define UPDATE_LOADER_FLOOR     3u

// The outcome, as docs/update-format.md names them
typedef enum {
    UPDATE_OK,
    UPDATE_NOT_AN_UPDATE,       // Wrong magic
    UPDATE_USB_INSTALL_FILE,    // A UF2 file: not_an_update, with its own message (D11)
    UPDATE_UNSUPPORTED_FORMAT,
    UPDATE_BAD_HEADER,
    UPDATE_WRONG_BOARD,
    UPDATE_NEEDS_USB_INSTALL,   // Another layout, or a newer loader needed
    UPDATE_BAD_SIZE,            // The upload isn't 256 + image_size + blob_size bytes
    UPDATE_NEEDS_BLOB,          // No blob in the file, and the paired Wi-Fi area holds another one
    UPDATE_BAD_IMAGE,           // The image's SHA-256, as received or read back
    UPDATE_BAD_BLOB,            // The blob's SHA-256, as received or read back
    UPDATE_INCOMPLETE,          // The upload stopped before its end
    UPDATE_FLASH_WRITE,         // Boot control couldn't be written before the first erase
} update_result_t;

// The API's error code for a result ("not_an_update", ...); "ok" for UPDATE_OK
const char *update_result_code(update_result_t r);

typedef struct {
    uint32_t flags;
    uint32_t layout;
    uint32_t loader;
    uint32_t image_size;
    uint32_t blob_size;
    char board[UPDATE_BOARD_MAX + 1];
    char version[UPDATE_VERSION_MAX + 1];
    uint8_t image_sha256[32];
    uint8_t blob_sha256[32];
} update_header_t;

// What the running firmware is and has
typedef struct {
    const char *board;          // PICO_BOARD
    uint32_t layout;            // LAYOUT_VERSION
    uint32_t loader;            // The installed loader's version (1 if it doesn't report one)
} update_env_t;

// Flash, SHA-256 and boot control for the writer. Offsets are from the start of flash.
typedef struct {
    void (*erase_sector)(void *ctx, uint32_t offset);
    void (*program)(void *ctx, uint32_t offset, const uint8_t *data, size_t length);   // Whole pages
    const uint8_t *(*read)(void *ctx, uint32_t offset);     // Flash as memory, from offset on
    // One SHA-256 at a time: start, update any number of times, finish
    void (*sha256_start)(void *ctx);
    void (*sha256_update)(void *ctx, const uint8_t *data, size_t length);
    void (*sha256_finish)(void *ctx, uint8_t out[32]);
    // Called once, before the first erase of the target slot or Wi-Fi area: boot control must stop
    // treating the slot as a firmware (it becomes empty). Returns false if that couldn't be stored.
    bool (*slot_emptied)(void *ctx, uint8_t slot);
    void *ctx;
} update_io_t;

// Checks a header (the first UPDATE_HEADER_SIZE bytes) against the format and the running firmware.
// `upload_length` is the whole upload's length (Content-Length).
update_result_t update_check_header(const uint8_t *raw, const update_env_t *env, uint32_t upload_length,
                                    const update_io_t *io, update_header_t *out);

// Writes the header for `h`, as tools/mkupdate.py does (the firmware backup, D42): bytes 0 to
// UPDATE_HEADER_HASHED - 1, and zeros after them, where the caller puts their SHA-256
void update_build_header(const update_header_t *h, uint8_t raw[UPDATE_HEADER_SIZE]);

// The length of the Wi-Fi area image at `area` (its "W43B" header and the data), or 0 if none
uint32_t update_wifi_image_size(const uint8_t *area);

// The writer: begin, feed the upload's bytes as they come (any chunk sizes), then finish
typedef enum { UPDATE_STAGE_HEADER, UPDATE_STAGE_IMAGE, UPDATE_STAGE_BLOB, UPDATE_STAGE_DONE } update_stage_t;

typedef struct {
    const update_io_t *io;
    update_env_t env;
    uint8_t slot;                       // The target: the slot that isn't running
    uint32_t upload_length;
    update_result_t result;             // UPDATE_OK while all is well; the first failure sticks
    update_stage_t stage;
    update_header_t header;
    uint8_t raw_header[UPDATE_HEADER_SIZE];
    uint32_t received;                  // Bytes of the upload so far
    bool write_blob;                    // The paired Wi-Fi area needs the file's blob
    bool emptied;                       // slot_emptied() was called
    uint32_t area_offset;               // Where the current part goes in flash
    uint32_t part_left;                 // Bytes of the current part still to come
    uint32_t written;                   // Bytes of the current part written to flash
    uint32_t sector_fill;
    uint8_t sector[4096];
    // The first sectors of the image and of the Wi-Fi area, erased first but programmed last of all,
    // once both hashes are right: the image's holds the block the boot ROM looks for, the area's the
    // header the firmware looks for. So a cut-off or damaged upload never leaves a slot the boot ROM
    // would start, or a Wi-Fi area that looks whole, whatever boot control says (phase 3 review;
    // phase 5 security review)
    uint32_t first_bytes;
    uint8_t first[4096];
    uint32_t blob_first_bytes;
    uint8_t blob_first[4096];
} update_t;

// Starts an upload into `slot` (BOOTCTL_SLOT_A or _B), the slot that isn't running
void update_begin(update_t *u, const update_io_t *io, const update_env_t *env, uint8_t slot, uint32_t upload_length);

// Takes the next bytes of the upload. Returns the result so far: once it isn't UPDATE_OK, the upload
// has failed and the rest of the bytes are ignored. Flash is erased and written as sectors fill.
update_result_t update_feed(update_t *u, const uint8_t *data, size_t length);

// After the last byte: writes what's left, then checks both hashes again as read back from flash.
// UPDATE_OK means the slot (and the Wi-Fi area, if it needed the blob) holds the new firmware.
update_result_t update_finish(update_t *u);
