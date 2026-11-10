/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "records.h"

uint32_t record_crc32(uint32_t crc, const void *data, size_t length) {
    const uint8_t *p = data;
    crc = ~crc;
    while (length--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static uint32_t record_crc(const record_header_t *rec, const void *payload) {
    uint32_t crc = record_crc32(0, rec, offsetof(record_header_t, crc));
    return record_crc32(crc, payload, rec->length);
}

static bool record_valid(const record_header_t *rec, uint32_t magic, uint16_t version, uint16_t length) {
    return rec->magic == magic && rec->version == version && rec->length == length &&
           length <= RECORD_MAX_PAYLOAD && rec->crc == record_crc(rec, record_payload(rec));
}

// Intact as a record of `magic`, whatever its version: the CRC covers the version and length it states
static bool record_intact(const record_header_t *rec, uint32_t magic) {
    return rec->magic == magic && rec->length <= RECORD_MAX_PAYLOAD && rec->crc == record_crc(rec, record_payload(rec));
}

static const record_header_t *sector(const uint8_t *area, int i) {
    return (const record_header_t *)(area + (size_t)i * RECORD_SECTOR_SIZE);
}

// The index (0 or 1) of the sector with the newest valid record, or -1
static int newest(const uint8_t *area, uint32_t magic, uint16_t version, uint16_t length) {
    bool v0 = record_valid(sector(area, 0), magic, version, length);
    bool v1 = record_valid(sector(area, 1), magic, version, length);
    if (v0 && v1) {
        // Wrap-around compare: the newer one is ahead by less than half the range
        return (int32_t)(sector(area, 1)->seq - sector(area, 0)->seq) > 0 ? 1 : 0;
    }
    return v0 ? 0 : (v1 ? 1 : -1);
}

// The sector a new record goes to (phase 2 review: firmwares of two versions, one per slot, D12):
// - one without an intact record of this magic;
// - else the one with the older record of this version;
// - else the older record of another version.
// A record of another version so survives as long as this version has a sector of its own. Only
// then does a write replace this version's record in place, without the power-cut safety of D23.
static int target_sector(const uint8_t *area, uint32_t magic, uint16_t version, uint16_t length) {
    const bool any0 = record_intact(sector(area, 0), magic), any1 = record_intact(sector(area, 1), magic);
    if (!any0 || !any1) {
        return any0 ? 1 : 0;
    }
    const bool own0 = record_valid(sector(area, 0), magic, version, length);
    const bool own1 = record_valid(sector(area, 1), magic, version, length);
    const int older = (int32_t)(sector(area, 1)->seq - sector(area, 0)->seq) > 0 ? 0 : 1;
    if (own0 != own1) {
        return own0 ? 0 : 1;
    }
    return older;
}

const record_header_t *record_find(const uint8_t *area, uint32_t magic, uint16_t version, uint16_t length,
                                   uint32_t *seq) {
    int i = newest(area, magic, version, length);
    if (i < 0) {
        return NULL;
    }
    if (seq) {
        *seq = sector(area, i)->seq;
    }
    return sector(area, i);
}

bool record_write(const uint8_t *area, const record_flash_t *flash, uint32_t magic, uint16_t version,
                  const void *payload, uint16_t length) {
    if (length > RECORD_MAX_PAYLOAD) {
        return false;
    }
    const int target = target_sector(area, magic, version, length);
    const int other = 1 - target;

    // The new record, padded with 0xFF to whole pages (programming 0xFF leaves flash erased)
    static uint8_t image[RECORD_SECTOR_SIZE];
    size_t size = sizeof(record_header_t) + length;
    size_t pages = (size + RECORD_PAGE_SIZE - 1) / RECORD_PAGE_SIZE;
    memset(image, 0xFF, pages * RECORD_PAGE_SIZE);

    record_header_t rec = {
        .magic = magic,
        .version = version,
        .length = length,
        // Ahead of whatever the other sector holds, of any version
        .seq = record_intact(sector(area, other), magic) ? sector(area, other)->seq + 1 : 1,
    };
    rec.crc = record_crc(&rec, payload);
    memcpy(image, &rec, sizeof(rec));
    memcpy(image + sizeof(rec), payload, length);

    uint32_t offset = (uint32_t)target * RECORD_SECTOR_SIZE;
    flash->erase_sector(flash->ctx, offset);
    flash->program(flash->ctx, offset, image, pages * RECORD_PAGE_SIZE);

    return newest(area, magic, version, length) == target;
}
