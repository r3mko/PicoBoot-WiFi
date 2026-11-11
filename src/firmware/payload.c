/**
 * Copyright (c) 2025-2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// From PicoBoot-DX v0.10 (validate_payload() in src/picoboot-dx.c), with the region as parameters.

#include <string.h>
#include "payload.h"
#include "endian.h"
#include "records.h"
#include "bootctl.h"

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

#define FINGERPRINT_SAMPLES 64u

uint32_t payload_fingerprint(const uint32_t *base, size_t region_bytes) {
    if (payload_check(base, region_bytes) == PAYLOAD_INVALID) {
        return 0;
    }
    const uint32_t words = BigEndian32(base[2]) / 4;       // At least 9: the header and the footer
    uint32_t crc = record_crc32(0, base, 8 * 4);
    for (uint32_t i = 0; i < FINGERPRINT_SAMPLES; i++) {
        crc = record_crc32(crc, &base[(uint32_t)((uint64_t)i * words / FINGERPRINT_SAMPLES)], 4);
    }
    const uint32_t tail = words < 64 ? words : 64;
    crc = record_crc32(crc, &base[words - tail], tail * 4);
    return crc ? crc : 1;
}

uint8_t payload_choose(const bootctl_t *b, bool stored, uint32_t fingerprint, bool *usb_install) {
    *usb_install = stored && fingerprint && fingerprint != b->payload1_fingerprint;
    return *usb_install ? BOOTCTL_PAYLOAD_1 : b->active_payload;
}

bool payload_record(bootctl_t *b, uint32_t fingerprint, bool usb_install, bool complete) {
    bool changed = false;
    if (usb_install) {
        if (b->active_payload != BOOTCTL_PAYLOAD_1 || b->payload_name[BOOTCTL_PAYLOAD_1][0]) {
            b->active_payload = BOOTCTL_PAYLOAD_1;
            memset(b->payload_name[BOOTCTL_PAYLOAD_1], 0, sizeof(b->payload_name[BOOTCTL_PAYLOAD_1]));
            changed = true;
        }
        if (complete) {
            b->payload1_fingerprint = fingerprint;
            changed = true;
        }
    } else if (fingerprint != b->payload1_fingerprint) {
        b->payload1_fingerprint = fingerprint;      // Not valid any more, or boot control was blank
        changed = true;
    }
    return changed;
}

uint8_t payload_upload_target(uint8_t active_payload) {
    return active_payload == BOOTCTL_PAYLOAD_2 ? BOOTCTL_PAYLOAD_1 : BOOTCTL_PAYLOAD_2;
}
