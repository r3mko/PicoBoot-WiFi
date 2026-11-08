/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "ringlog.h"

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint16_t version;
    uint16_t length;
} ringlog_header_t;

_Static_assert(sizeof(ringlog_header_t) == RINGLOG_HEADER_SIZE, "header size");

#define ENTRIES_PER_SECTOR (RECORD_SECTOR_SIZE / RINGLOG_ENTRY_SIZE)

static const uint8_t *entry(const ringlog_t *log, unsigned i) {
    return log->area + (size_t)i * RINGLOG_ENTRY_SIZE;
}

static unsigned entry_count(const ringlog_t *log) {
    return log->size / RINGLOG_ENTRY_SIZE;
}

static uint32_t entry_crc(const uint8_t *e) {
    return record_crc32(0, e, RINGLOG_ENTRY_SIZE - 4);
}

static bool valid(const uint8_t *e, uint32_t magic, uint16_t version, uint16_t length) {
    ringlog_header_t h;
    uint32_t crc;
    memcpy(&h, e, sizeof(h));
    memcpy(&crc, e + RINGLOG_ENTRY_SIZE - 4, sizeof(crc));
    return h.magic == magic && h.version == version && h.length == length && crc == entry_crc(e);
}

static bool erased(const uint8_t *e) {
    for (unsigned i = 0; i < RINGLOG_ENTRY_SIZE; i++) {
        if (e[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

static uint32_t seq_of(const uint8_t *e) {
    ringlog_header_t h;
    memcpy(&h, e, sizeof(h));
    return h.seq;
}

// Index of the newest valid entry (wrap-around compare of sequence numbers), or -1
static int newest(const ringlog_t *log, uint32_t magic, uint16_t version, uint16_t length) {
    int best = -1;
    for (unsigned i = 0; i < entry_count(log); i++) {
        if (valid(entry(log, i), magic, version, length) &&
            (best < 0 || (int32_t)(seq_of(entry(log, i)) - seq_of(entry(log, (unsigned)best))) > 0)) {
            best = (int)i;
        }
    }
    return best;
}

bool ringlog_append(const ringlog_t *log, uint32_t magic, uint16_t version, const void *payload, uint16_t length) {
    if (length > RINGLOG_MAX_PAYLOAD || log->size < 2 * RECORD_SECTOR_SIZE || log->size % RECORD_SECTOR_SIZE) {
        return false;
    }
    const unsigned count = entry_count(log);
    const int last = newest(log, magic, version, length);
    unsigned next = last < 0 ? 0 : ((unsigned)last + 1) % count;

    if (!erased(entry(log, next))) {
        if (next % ENTRIES_PER_SECTOR != 0) {
            // A torn or stray page in the middle of a sector: move on to the next sector rather
            // than erase this one, which holds the newest entries
            next = (next / ENTRIES_PER_SECTOR + 1) * ENTRIES_PER_SECTOR % count;
        }
        log->flash->erase_sector(log->flash->ctx, next / ENTRIES_PER_SECTOR * RECORD_SECTOR_SIZE);
    }

    uint8_t page[RINGLOG_ENTRY_SIZE];
    memset(page, 0xFF, sizeof(page));
    const ringlog_header_t h = {magic, last < 0 ? 1 : seq_of(entry(log, (unsigned)last)) + 1, version, length};
    memcpy(page, &h, sizeof(h));
    memcpy(page + sizeof(h), payload, length);
    const uint32_t crc = entry_crc(page);
    memcpy(page + RINGLOG_ENTRY_SIZE - 4, &crc, sizeof(crc));

    log->flash->program(log->flash->ctx, next * RINGLOG_ENTRY_SIZE, page, sizeof(page));
    return newest(log, magic, version, length) == (int)next;
}

unsigned ringlog_read(const ringlog_t *log, uint32_t magic, uint16_t version, uint16_t length,
                      void *out, uint32_t *seq, unsigned max) {
    const int top = newest(log, magic, version, length);
    if (top < 0) {
        return 0;
    }
    const uint32_t top_seq = seq_of(entry(log, (unsigned)top));

    // One pass over the area: the valid entries and their age relative to the newest (so
    // wrap-around and gaps from torn entries don't matter). Each entry's CRC is checked once.
    uint8_t index[RINGLOG_MAX_ENTRIES];
    uint32_t age[RINGLOG_MAX_ENTRIES];
    unsigned found = 0;
    for (unsigned j = 0; j < entry_count(log) && j < RINGLOG_MAX_ENTRIES; j++) {
        if (valid(entry(log, j), magic, version, length)) {
            index[found] = (uint8_t)j;
            age[found] = top_seq - seq_of(entry(log, j));
            found++;
        }
    }

    // Newest first: each time, the youngest of the entries not yet taken
    unsigned n = 0;
    for (; n < max && n < found; n++) {
        unsigned pick = n;
        for (unsigned k = n + 1; k < found; k++) {
            if (age[k] < age[pick]) {
                pick = k;
            }
        }
        const uint8_t i = index[pick];
        const uint32_t a = age[pick];
        index[pick] = index[n];
        age[pick] = age[n];
        index[n] = i;
        age[n] = a;
        memcpy((uint8_t *)out + (size_t)n * length, entry(log, i) + RINGLOG_HEADER_SIZE, length);
        if (seq) {
            seq[n] = seq_of(entry(log, i));
        }
    }
    return n;
}
