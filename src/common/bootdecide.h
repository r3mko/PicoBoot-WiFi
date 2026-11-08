/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The loader's decision as one pure function (design D7, D8), so host tests can cover it.
// Phase 3 extends it with the trial, fallback and crash-loop rules (D12).

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bootctl.h"

typedef struct {
    bool had_por;               // POWMAN CHIP_RESET.HAD_POR (sticky across warm reboots: phase 0)
    bool marker_set;            // The session marker was already in POWMAN scratch
    const bootctl_t *bootctl;   // The newest valid boot control record, or NULL
} boot_inputs_t;

typedef struct {
    bool power_on;              // Inject at this boot
    uint8_t slots[2];           // The slots to try, in this order (BOOTCTL_SLOT_A / _B)
    bool bootctl_default;       // No valid boot control: the defaults were used
} boot_decision_t;

boot_decision_t boot_decide(const boot_inputs_t *in);
