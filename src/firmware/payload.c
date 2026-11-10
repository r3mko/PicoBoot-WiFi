/**
 * Copyright (c) 2025-2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// From PicoBoot-DX v0.10 (validate_payload() in src/picoboot-dx.c), with the region as parameters.

#include "payload.h"
#include "endian.h"
#include "records.h"

size_t payload_check(const uint32_t *base, size_t region_bytes) {
    const uint32_t *p = base;
    const size_t region_words = region_bytes / sizeof(*p);

    if (region_words < 3 ||
        BigEndian32(p[0]) != 0x49504C42 ||   // "IPLB"
        BigEndian32(p[1]) != 0x4F4F5420) {   // "OOT "
        return PAYLOAD_INVALID;
    }

    uint32_t raw_size_bytes = BigEndian32(p[2]);

    // Size includes the 32-byte header and 4-byte footer, and must fit in the payload region
    if (raw_size_bytes < 36 || raw_size_bytes > region_bytes) {
        return PAYLOAD_INVALID;
    }

    const size_t word_count = raw_size_bytes / sizeof(*p);
    const size_t alignment = 1024 / sizeof(*p);
    const size_t word_count_aligned = (word_count + alignment - 1) / alignment * alignment;

    if (word_count_aligned > region_words) {
        return PAYLOAD_INVALID;
    }

    if (BigEndian32(p[word_count - 1]) != 0x5049434F) {   // "PICO"
        return PAYLOAD_INVALID;
    }

    return word_count_aligned;
}

uint32_t payload_fingerprint(const uint32_t *base, size_t region_bytes) {
    if (payload_check(base, region_bytes) == PAYLOAD_INVALID) {
        return 0;
    }
    const uint32_t size = BigEndian32(base[2]);
    const size_t head = size < 256 ? size : 256;
    uint32_t crc = record_crc32(0, base, head);
    crc = record_crc32(crc, &base[size / 4 - 1], 4);
    return crc ? crc : 1;
}
