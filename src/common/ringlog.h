/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// A ring of fixed-size entries over whole flash sectors, for the boot log (design D39). Each entry
// is one 256-byte page: a header (magic, sequence number, version, length), the payload, and a
// CRC-32 in the last four bytes. Appending goes to the page after the newest valid entry; a sector
// is erased only when the ring starts on it, and a torn page in the middle of a sector makes the
// ring move on to the next sector, so newer entries are never erased (phase 0 review finding 2).
//
// Plain C without SDK calls, flash access through record_flash_t (records.h), for host tests.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "records.h"

#define RINGLOG_ENTRY_SIZE    RECORD_PAGE_SIZE
#define RINGLOG_HEADER_SIZE   12u
#define RINGLOG_MAX_PAYLOAD   (RINGLOG_ENTRY_SIZE - RINGLOG_HEADER_SIZE - 4u)

typedef struct {
    const uint8_t *area;            // The log's sectors, readable
    uint32_t size;                  // Bytes, a whole number of sectors (at least two)
    const record_flash_t *flash;    // Offsets from the start of the area
} ringlog_t;

// Appends an entry; returns true if it reads back as the newest valid entry
bool ringlog_append(const ringlog_t *log, uint32_t magic, uint16_t version, const void *payload, uint16_t length);

// Copies up to `max` payloads (each `length` bytes) of valid entries into `out`, newest first, and
// their sequence numbers into `seq` (if not NULL). Returns how many.
unsigned ringlog_read(const ringlog_t *log, uint32_t magic, uint16_t version, uint16_t length,
                      void *out, uint32_t *seq, unsigned max);
