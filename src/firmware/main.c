/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The firmware, started by the loader from slot A or B (design D7).
//
// Steps 1.2-1.4 of the implementation plan: the injection of PicoBoot-DX with the glitch
// protection (D36), measured by the passive counters (D31) into a boot report. Later steps add
// the power-on rule and boot control (1.7) and Wi-Fi with the report page (1.8).

#include "pico/stdlib.h"
#include "pico/platform.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/powman.h"
#include "layout.h"
#include "ipl.h"
#include "payload.h"
#include "injection.h"
#include "counters.h"
#include "report.h"

// Payload 1, read through the untranslated XIP window: the loader maps only this firmware's
// slot at XIP_BASE, so the rest of flash isn't reachable there (boot ROM research §4)
#define PAYLOAD1 ((const uint32_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + LAYOUT_PAYLOAD1_OFFSET))

#define FIRST_CS_TIMEOUT_US  (3 * 1000 * 1000)      // No CS by then: no console (bench)
#define INJECTION_TIMEOUT_US (10 * 1000 * 1000)     // Swiss (1.3 MB) takes about 0.4 s

static boot_report_t report;

// Runs the injection and measures it into the report. Nothing slow may run before "PIO ready",
// and nothing may write flash until the injection has ended (design C1, C6).
static void inject(const injection_payload_t *payload) {
    const uint64_t t_main = time_us_64();

    injection_prepare(payload);
    counters_init();
    counters_start();           // The counters run before the injection, so they see every edge
    injection_go();

    const uint64_t t_ready = time_us_64();
    report.cs_high_at_ready = gpio_get(INJECTION_PIN_CS);
    report.main_to_ready_us = (uint32_t)(t_ready - t_main);
    report.margin_raw_us = report.margin_clocked_us = -1;

    while (!counters_first_raw_seen() && time_us_64() - t_ready < FIRST_CS_TIMEOUT_US) {
        tight_loop_contents();
    }

    if (counters_first_raw_seen()) {
        const uint64_t t_first = time_us_64();
        report.margin_raw_us = (int32_t)(t_first - t_ready);
        report.result = INJECTION_STALLED;
        while (time_us_64() - t_first < INJECTION_TIMEOUT_US) {
            if (report.margin_clocked_us < 0 && counters_first_clocked_seen()) {
                report.margin_clocked_us = (int32_t)(time_us_64() - t_ready);
            }
            if (injection_complete()) {
                report.result = INJECTION_COMPLETE;
                break;
            }
        }
        report.injection_us = (uint32_t)(time_us_64() - t_first);
    } else {
        report.result = INJECTION_NO_CS;
    }

    counters_snapshot(&report.cs_raw, &report.cs_clocked);
    report.words_delivered = (uint32_t)(payload->count - injection_words_left());
    injection_finish();
}

int main(void) {
    report.chip_reset = powman_hw->chip_reset;
    report.power_on = true;     // Step 1.7: the loader's decision (design D8)

    // A valid payload 1 is sent with DMA byte swap (stored big-endian); otherwise the built-in IPL
    injection_payload_t payload = {PAYLOAD1, payload_check(PAYLOAD1, LAYOUT_PAYLOAD1_SIZE), true};
    report.payload = PAYLOAD_SOURCE_PAYLOAD1;
    if (payload.count == PAYLOAD_INVALID) {
        payload = (injection_payload_t){ipl, count_of(ipl), false};
        report.payload = PAYLOAD_SOURCE_BUILTIN;
    }
    report.payload_words = (uint32_t)payload.count;

    inject(&payload);

    report.chip_version = rp2350_chip_version();
    report.rom_version = rp2350_rom_version();

    // Idle
    while (true) {
        tight_loop_contents();
    }
}
