/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Payload uploads (design D15, D16, D42): a payload UF2 (any family, blocks from 0x10080000 on) or a
// raw payload ("IPLBOOT " ... "PICO"), streamed into a payload slot. Like update.c: flash and SHA-256
// come in through payloadup_io_t, so this is plain C for host tests.

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOADUP_UF2_BASE  0x10080000u     // Where payload UF2s put the payload (design C3)

typedef enum {
    PAYLOADUP_OK,
    PAYLOADUP_NOT_A_PAYLOAD,    // Neither a UF2 nor a raw payload, or no "IPLBOOT " header
    PAYLOADUP_BAD_UF2,          // A broken block, or the payload's blocks not in order from 0x10080000
    PAYLOADUP_TOO_LARGE,        // The header's size, or the data, is more than the slot holds
    PAYLOADUP_BAD_PAYLOAD,      // The size field, the "PICO" footer, or less data than the size says
    PAYLOADUP_BAD_READBACK,     // What flash holds afterwards doesn't match what came in
    PAYLOADUP_INCOMPLETE,       // The upload stopped before its end
    PAYLOADUP_FLASH_WRITE,      // Boot control couldn't be written before the first erase
} payloadup_result_t;

// The API's error code for a result ("not_a_payload", ...); "ok" for PAYLOADUP_OK
const char *payloadup_result_code(payloadup_result_t r);

typedef struct {
    void (*erase_sector)(void *ctx, uint32_t offset);
    void (*program)(void *ctx, uint32_t offset, const uint8_t *data, size_t length);   // Whole pages
    const uint8_t *(*read)(void *ctx, uint32_t offset);
    void (*sha256_start)(void *ctx);
    void (*sha256_update)(void *ctx, const uint8_t *data, size_t length);
    void (*sha256_finish)(void *ctx, uint8_t out[32]);
    // Once, before the first erase: the slot stops being a payload in boot control (its name goes)
    bool (*slot_emptied)(void *ctx);
    void *ctx;
} payloadup_io_t;

typedef struct {
    const payloadup_io_t *io;
    uint32_t offset, size;              // The slot in flash
    uint32_t upload_length;
    uint32_t received;                  // Bytes of the upload so far
    payloadup_result_t result;          // PAYLOADUP_OK while all is well; the first failure sticks
    enum { FORMAT_UNKNOWN, FORMAT_RAW, FORMAT_UF2 } format;
    // UF2: the 512-byte block being collected, and the payload's family and next address
    uint8_t block[512];
    uint32_t block_fill;
    bool family_known;
    uint32_t family;
    uint32_t next_address;
    // The payload bytes: their count, the header's size, the footer seen, and the flash writing
    uint32_t payload_bytes;
    uint32_t payload_size;              // From the header, 0 until known
    uint8_t head[12];
    bool emptied;
    uint32_t written;                   // Bytes of the slot erased (whole sectors)
    uint32_t sector_fill;
    uint8_t sector[4096];
    uint32_t first_bytes;               // The first sector, programmed last (as in update.c)
    uint8_t first[4096];
    uint8_t sha256[32];                 // Of the payload's size bytes, when done
} payloadup_t;

// Starts an upload into the slot at `offset`, `size` bytes long
void payloadup_begin(payloadup_t *u, const payloadup_io_t *io, uint32_t offset, uint32_t size, uint32_t upload_length);

// Takes the next bytes of the upload (any chunk sizes); returns the result so far
payloadup_result_t payloadup_feed(payloadup_t *u, const uint8_t *data, size_t length);

// After the last byte: writes what's left and the first sector, then checks the payload as flash
// holds it (payload_check()) and its SHA-256 read back. On PAYLOADUP_OK, u->payload_size and
// u->sha256 describe it. On a failure after the first erase, the slot is left without a header.
payloadup_result_t payloadup_finish(payloadup_t *u);
