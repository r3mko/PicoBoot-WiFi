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
    int current = newest(area, magic, version, length);
    int target = (current == 0) ? 1 : 0;

    // The new record, padded with 0xFF to whole pages (programming 0xFF leaves flash erased)
    static uint8_t image[RECORD_SECTOR_SIZE];
    size_t size = sizeof(record_header_t) + length;
    size_t pages = (size + RECORD_PAGE_SIZE - 1) / RECORD_PAGE_SIZE;
    memset(image, 0xFF, pages * RECORD_PAGE_SIZE);

    record_header_t rec = {
        .magic = magic,
        .version = version,
        .length = length,
        .seq = current < 0 ? 1 : sector(area, current)->seq + 1,
    };
    rec.crc = record_crc(&rec, payload);
    memcpy(image, &rec, sizeof(rec));
    memcpy(image + sizeof(rec), payload, length);

    uint32_t offset = (uint32_t)target * RECORD_SECTOR_SIZE;
    flash->erase_sector(flash->ctx, offset);
    flash->program(flash->ctx, offset, image, pages * RECORD_PAGE_SIZE);

    return newest(area, magic, version, length) == target;
}
