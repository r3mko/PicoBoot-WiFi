/**
 * Copyright (c) 2025-2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bootctl.h"

#define PAYLOAD_INVALID 0

// Checks a payload stored in flash in the PicoBoot format (big-endian words):
//   [0] = "IPLB", [1] = "OOT ", [2] = total size in bytes (32-byte header + image + 4-byte footer)
//   ...scrambled image...
//   [size / 4 - 1] = "PICO"
// The payload is sent in whole 1K transfers, so the size rounded up to 1K must also fit.
// Returns the number of words to send (a multiple of 256), or PAYLOAD_INVALID.
size_t payload_check(const uint32_t *base, size_t region_bytes);

// A short fingerprint of a payload in flash (design D17): a CRC-32 of its header (with the size), of
// 64 words spread over the whole payload, and of its last 256 bytes (with the footer). Builds of a
// payload share much of their start, so the samples tell another build of the same size apart; a
// payload written again unchanged can't be. Quick enough to compare before the injection (C1). 0 if
// the region holds no valid payload; never 0 otherwise.
uint32_t payload_fingerprint(const uint32_t *base, size_t region_bytes);

// D17 before the injection: the payload slot to inject (BOOTCTL_PAYLOAD_*), from boot control `b`,
// whether it was stored (false: the defaults, after a USB install of the firmware, where payload 1
// is the choice anyway) and payload 1's fingerprint now. *usb_install: payload 1 changed since boot
// control recorded it, so a USB install wrote it, and it boots whatever the choice.
uint8_t payload_choose(const bootctl_t *b, bool stored, uint32_t fingerprint, bool *usb_install);

// D17 after the injection, or its skip: what boot control keeps of this power-on. A USB install makes
// payload 1 the choice, without a name, at once; its fingerprint is recorded only after a complete
// injection, so until a console took it every power-on counts as the first after the USB install,
// and the page says so at the one the user sees (the drag-and-drop reboots the Pico on USB power,
// without a console). Returns whether `b` changed, to be stored.
bool payload_record(bootctl_t *b, uint32_t fingerprint, bool usb_install, bool complete);

// Where an upload goes (D15, D42): the payload slot that isn't the choice (BOOTCTL_PAYLOAD_*), and
// payload 2 while the built-in is the choice, so payload 1 keeps what a USB install put there
uint8_t payload_upload_target(uint8_t active_payload);
