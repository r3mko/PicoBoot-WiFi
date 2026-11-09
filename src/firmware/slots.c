/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "slots.h"
#include "bootinfo.h"

static uint8_t other(uint8_t slot) {
    return slot == BOOTCTL_SLOT_A ? BOOTCTL_SLOT_B : BOOTCTL_SLOT_A;
}

bool slots_at_start(bootctl_t *b, uint8_t running, uint32_t loader_flags) {
    const uint8_t o = other(running);
    bool changed = false;
    if ((loader_flags & BOOTINFO_TRIAL_FAILED) && b->pending_slot == o) {
        b->pending_slot = BOOTCTL_NONE;
        b->slot_state[o] = BOOTCTL_STATE_FAILED;
        changed = true;
    }
    if ((loader_flags & BOOTINFO_CRASH_FALLBACK) && b->active_slot != running) {
        b->active_slot = running;           // For good (the owner's choice)
        b->slot_state[o] = BOOTCTL_STATE_FAILED;
        if (b->pending_slot == o) {
            b->pending_slot = BOOTCTL_NONE;
        }
        changed = true;
    }
    return changed;
}

bool slots_keep(bootctl_t *b, uint8_t running) {
    if (b->pending_slot != running) {
        return false;
    }
    const uint8_t replaced = b->active_slot;
    b->active_slot = running;
    b->slot_state[running] = BOOTCTL_STATE_KEPT;
    b->pending_slot = BOOTCTL_NONE;
    if (replaced != running && b->slot_state[replaced] == BOOTCTL_STATE_INSTALLED) {
        b->slot_state[replaced] = BOOTCTL_STATE_KEPT;
    }
    return true;
}

slots_switch_t slots_switch(bootctl_t *b, uint8_t running, uint8_t target) {
    const uint8_t state = b->slot_state[target];
    if (state == BOOTCTL_STATE_EMPTY) {
        return SLOTS_NOT_BOOTABLE;
    }
    if (target == running) {
        return SLOTS_NOTHING_TO_DO;     // On trial: Keep is the way to keep it
    }
    if (state == BOOTCTL_STATE_KEPT || state == BOOTCTL_STATE_INSTALLED) {
        b->active_slot = target;
        if (b->pending_slot == running) {
            b->pending_slot = BOOTCTL_NONE;     // Abandoned: it stays in the trial state, unkept
        }
    } else {
        b->pending_slot = target;               // Another try, on trial: the loader starts it
        b->slot_state[target] = BOOTCTL_STATE_TRIAL;
    }
    return SLOTS_SWITCHED;
}
