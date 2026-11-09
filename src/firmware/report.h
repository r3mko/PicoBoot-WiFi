/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

// What happened at this boot (design D22 diagnostics, D31), kept in RAM for the report page.

typedef enum {
    PAYLOAD_SOURCE_NONE = 0,    // No injection (not a power-on)
    PAYLOAD_SOURCE_PAYLOAD1,
    PAYLOAD_SOURCE_PAYLOAD2,
    PAYLOAD_SOURCE_BUILTIN,
} payload_source_t;

typedef enum {
    INJECTION_NOT_RUN = 0,      // Not a power-on: no injection (design C2, D8)
    INJECTION_NO_CS,            // The console's first CS didn't come (no console: on the bench)
    INJECTION_COMPLETE,         // All words sent and the data line released
    INJECTION_STALLED,          // First CS seen, but not complete in time
} injection_result_t;

typedef struct {
    uint32_t chip_reset;        // POWMAN CHIP_RESET (HAD_POR is sticky across warm reboots: phase 0)
    uint8_t chip_version;       // RP2350 stepping: 2 = A2 (RP2350-E9), 3 = A3, 4 = A4
    uint8_t rom_version;        // Boot ROM version
    bool power_on;              // The power-on decision (design D8)
    bool from_loader;           // The decision came from the loader (else: no loader, own D8 check)
    uint8_t slot;               // The slot this firmware runs from (BOOTCTL_SLOT_A / _B)
    bool fallback;              // The loader's first choice held no valid image
    bool bootctl_default;       // No valid boot control record: defaults used
    uint8_t loader_flags;       // BOOTINFO_TRIAL, _TRIAL_FAILED, _CRASH_FALLBACK from the loader (D12)
    uint8_t loader_version;     // The installed loader's version (1 if it reports none)

    payload_source_t payload;
    uint8_t payload_chosen;     // Boot control's choice (BOOTCTL_PAYLOAD_*), BOOTCTL_NONE if no injection;
                                // a different `payload` means the chosen one wasn't valid (D22)
    uint32_t payload_words;     // Words to send (256 per 1K transfer)
    uint32_t words_delivered;
    injection_result_t result;

    uint32_t main_to_ready_us;  // main() to "PIO ready"
    int32_t margin_raw_us;      // "PIO ready" to the first CS falling edge, -1 if none
    int32_t margin_clocked_us;  // "PIO ready" to the first real (clocked) transfer, -1 if none
    uint32_t injection_us;      // First CS to the end of the injection
    uint32_t cs_raw;            // CS falling edges counted at the end of the injection
    uint32_t cs_clocked;        // Real transfers counted then; fewer than cs_raw: a glitch (D36)
    bool cs_high_at_ready;      // CS level at "PIO ready" (high with the D36 pull-up)
    bool cs_pulldown_test;      // Test mode: PicoBoot's CS pull-down instead of the pull-up
} boot_report_t;
