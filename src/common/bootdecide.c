/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "bootdecide.h"

boot_decision_t boot_decide(const boot_inputs_t *in) {
    boot_decision_t d = {0};

    // Design D8: a power-on only when HAD_POR is set and our session marker isn't. HAD_POR alone
    // stays set after warm reboots (phase 0), and a brown-out (HAD_BOR) or RUN reset doesn't count:
    // the console may still be running then.
    d.power_on = in->had_por && !in->marker_set;

    const bootctl_t *b = in->bootctl;
    d.bootctl_default = (b == NULL);
    uint8_t active = b ? b->active_slot : BOOTCTL_DEFAULT.active_slot;
    if (active != BOOTCTL_SLOT_A && active != BOOTCTL_SLOT_B) {
        active = BOOTCTL_SLOT_A;
    }

    // The active slot first; the other one if it holds no valid image
    d.slots[0] = active;
    d.slots[1] = (active == BOOTCTL_SLOT_A) ? BOOTCTL_SLOT_B : BOOTCTL_SLOT_A;
    return d;
}
