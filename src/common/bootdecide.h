/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The loader's decision as one pure function (design D7, D8, D12), so host tests can cover it.

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bootctl.h"

typedef struct {
    bool had_por;               // POWMAN CHIP_RESET.HAD_POR (sticky across warm reboots: phase 0)
    bool marker_set;            // The session marker was already in POWMAN scratch
    const bootctl_t *bootctl;   // The newest valid boot control record, or NULL
    uint32_t session;           // BOOTINFO_SCRATCH_SESSION as found (0 after a power-on)
    bool clean_reboot;          // BOOTINFO_SCRATCH_REBOOT held BOOTINFO_CLEAN_REBOOT
} boot_inputs_t;

typedef struct {
    bool power_on;              // Inject at this boot
    bool bootctl_default;       // No valid boot control: the defaults were used
    // The slots to try, in this order (the second if the first holds no valid image); BOOTCTL_NONE
    // for no slot. For each: the BOOTINFO_TRIAL / _TRIAL_FAILED / _CRASH_FALLBACK flags to pass on,
    // and the session word to store before starting it.
    uint8_t slots[2];
    uint32_t flags[2];
    uint32_t session[2];
} boot_decision_t;

boot_decision_t boot_decide(const boot_inputs_t *in);
