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

// A short fingerprint of a payload in flash (design D17): a CRC-32 of its first 256 bytes, which
// hold the header and the size, and of its footer. Quick enough to compare before the injection
// (C1). 0 if the region holds no valid payload; never 0 otherwise.
uint32_t payload_fingerprint(const uint32_t *base, size_t region_bytes);

// Where an upload goes (D15, D42): the payload slot that isn't the choice (BOOTCTL_PAYLOAD_*), and
// payload 2 while the built-in is the choice, so payload 1 keeps what a USB install put there
uint8_t payload_upload_target(uint8_t active_payload);
