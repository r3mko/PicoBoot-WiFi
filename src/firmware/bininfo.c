/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "bininfo.h"

// From the SDK's pico/binary_info/defs.h and structure.h
#define MARKER_START        0x7188ebf2u
#define MARKER_END          0xe71aa390u
#define TYPE_ID_AND_STRING  6u
#define TAG_RASPBERRY_PI    0x5052u         // 'R', 'P'
#define ID_VERSION_STRING   0x11a9bc3au
#define LINK_BASE           0x10000000u     // Every slot image is built to run here (D7)
#define SEARCH              4096u           // The header sits in the image's first sector

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Offset in the image of `size` bytes at this run-time address, if `bytes` from there fit
static bool to_offset(uint32_t addr, uint32_t bytes, size_t size, uint32_t *off) {
    if (addr < LINK_BASE || addr - LINK_BASE > size || bytes > size - (addr - LINK_BASE)) {
        return false;
    }
    *off = addr - LINK_BASE;
    return true;
}

bool bininfo_version(const uint8_t *image, size_t size, char *out, size_t out_size) {
    if (out_size == 0) {
        return false;
    }
    const size_t search = size < SEARCH ? size : SEARCH;
    for (size_t h = 0; h + 20 <= search; h += 4) {
        if (le32(image + h) != MARKER_START || le32(image + h + 16) != MARKER_END) {
            continue;
        }
        const uint32_t start = le32(image + h + 4), end = le32(image + h + 8);
        uint32_t off;
        if (end < start || !to_offset(start, end - start, size, &off)) {
            return false;
        }
        for (uint32_t p = 0; p + 4 <= end - start; p += 4) {
            uint32_t e;
            if (!to_offset(le32(image + off + p), 16, size, &e)) {
                continue;               // An entry outside flash (or in RAM): skip it
            }
            const uint8_t *entry = image + e;
            if ((le32(entry) & 0xFFFFu) != TYPE_ID_AND_STRING || (le32(entry) >> 16) != TAG_RASPBERRY_PI ||
                le32(entry + 4) != ID_VERSION_STRING) {
                continue;
            }
            uint32_t s;
            if (!to_offset(le32(entry + 8), 1, size, &s)) {
                return false;
            }
            size_t n = 0;
            while (s + n < size && image[s + n] && n + 1 < out_size) {
                const uint8_t c = image[s + n];
                out[n++] = c >= 0x20 && c <= 0x7E ? (char)c : '?';
            }
            out[n] = '\0';
            return n > 0;
        }
        return false;
    }
    return false;
}
