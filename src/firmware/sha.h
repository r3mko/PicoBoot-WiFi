/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The RP2350's SHA-256 hardware, shared by the update and payload writers and the status page. One
// hash at a time: a new start releases a hash that was left unfinished (an upload cut off), so it
// never waits for the hardware forever (phase 3 review).

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void sha_start(void);
void sha_update(const uint8_t *data, size_t length);
void sha_finish(uint8_t out[32]);
void sha_release(void);         // Drops a hash in progress, if any

// A hash is in progress (an upload's): others must wait for it to finish
bool sha_busy(void);

// One hash over `length` bytes
void sha_of(const uint8_t *data, size_t length, uint8_t out[32]);
