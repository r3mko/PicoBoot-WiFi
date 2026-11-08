/**
 * Copyright (c) 2025-2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_INVALID 0

// Checks a payload stored in flash in the PicoBoot format (big-endian words):
//   [0] = "IPLB", [1] = "OOT ", [2] = total size in bytes (32-byte header + image + 4-byte footer)
//   ...scrambled image...
//   [size / 4 - 1] = "PICO"
// The payload is sent in whole 1K transfers, so the size rounded up to 1K must also fit.
// Returns the number of words to send (a multiple of 256), or PAYLOAD_INVALID.
size_t payload_check(const uint32_t *base, size_t region_bytes);
