/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "sha.h"
#include "pico/sha256.h"

static pico_sha256_state_t state;
static bool running;

void sha_release(void) {
    if (running) {
        pico_sha256_cleanup(&state);
        running = false;
    }
}

void sha_start(void) {
    sha_release();
    pico_sha256_start_blocking(&state, SHA256_BIG_ENDIAN, false);     // Without DMA
    running = true;
}

void sha_update(const uint8_t *data, size_t length) {
    pico_sha256_update_blocking(&state, data, length);
}

void sha_finish(uint8_t out[32]) {
    sha256_result_t result;
    pico_sha256_finish(&state, &result);
    running = false;
    memcpy(out, result.bytes, 32);
}

bool sha_busy(void) {
    return running;
}

void sha_of(const uint8_t *data, size_t length, uint8_t out[32]) {
    sha_start();
    sha_update(data, length);
    sha_finish(out);
}
