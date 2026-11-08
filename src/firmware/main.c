/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The firmware, started by the loader from slot A or B (design D7).
//
// Step 1.2 of the implementation plan: the injection of PicoBoot-DX, with payload 1 or the
// built-in gekkoboot. Later steps add the glitch protection (1.3), the counters (1.4), the
// power-on rule and boot control (1.7) and Wi-Fi (1.8).

#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "layout.h"
#include "ipl.h"
#include "payload.h"
#include "injection.h"

// Payload 1, read through the untranslated XIP window: the loader maps only this firmware's
// slot at XIP_BASE, so the rest of flash isn't reachable there (boot ROM research §4)
#define PAYLOAD1 ((const uint32_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + LAYOUT_PAYLOAD1_OFFSET))

int main(void) {
    // A valid payload 1 is sent with DMA byte swap (stored big-endian); otherwise the built-in IPL
    injection_payload_t payload = {PAYLOAD1, payload_check(PAYLOAD1, LAYOUT_PAYLOAD1_SIZE), true};
    if (payload.count == PAYLOAD_INVALID) {
        payload = (injection_payload_t){ipl, count_of(ipl), false};
    }

    injection_start(&payload);
    while (!injection_complete()) {
        tight_loop_contents();
    }
    injection_finish();

    // Idle
    while (true) {
        tight_loop_contents();
    }
}
