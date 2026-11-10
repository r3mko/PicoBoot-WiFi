/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Records that survive power loss (design D23): boot control and settings each have an area of
// two flash sectors with one record per sector. A write always goes to the sector that does not
// hold the newest valid record, so a power cut during a write can only damage the older copy.
// The newest valid record wins: right magic, version and length, a good CRC-32, and the highest
// sequence number (compared with wrap-around).
//
// Plain C without SDK calls, so the loader, the firmware and the host tests can all use it.

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECORD_SECTOR_SIZE  4096u
#define RECORD_PAGE_SIZE    256u
#define RECORD_AREA_SIZE    (2 * RECORD_SECTOR_SIZE)

typedef struct {
    uint32_t magic;             // Which record this is (boot control, settings)
    uint16_t version;           // Format version of the payload
    uint16_t length;            // Payload bytes after this header
    uint32_t seq;               // Counts up with every write
    uint32_t crc;               // CRC-32 of the header fields above and the payload
} record_header_t;

#define RECORD_MAX_PAYLOAD  (RECORD_SECTOR_SIZE - sizeof(record_header_t))

// Flash access for record_write(); offsets are from the start of the record area
typedef struct {
    void (*erase_sector)(void *ctx, uint32_t offset);
    void (*program)(void *ctx, uint32_t offset, const uint8_t *data, size_t length);   // Whole pages
    void *ctx;
} record_flash_t;

// The newest valid record in the area (the two sectors, readable through `area`), or NULL.
// On success, *seq gets its sequence number (if seq isn't NULL).
const record_header_t *record_find(const uint8_t *area, uint32_t magic, uint16_t version, uint16_t length,
                                   uint32_t *seq);

// The payload of a record found by record_find()
static inline const void *record_payload(const record_header_t *rec) {
    return rec + 1;
}

// Writes a new record into the sector that doesn't hold the newest valid one, then reads it back.
// Returns true if the new record is now the newest valid record. While the other sector holds a
// record of another version (another firmware's), it's left alone (see target_sector()).
bool record_write(const uint8_t *area, const record_flash_t *flash, uint32_t magic, uint16_t version,
                  const void *payload, uint16_t length);

// CRC-32 (IEEE 802.3, as zlib), also used by other modules
uint32_t record_crc32(uint32_t crc, const void *data, size_t length);
