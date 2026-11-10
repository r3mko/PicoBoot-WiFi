/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "bootdecide.h"
#include "bootinfo.h"

static uint8_t other(uint8_t slot) {
    return slot == BOOTCTL_SLOT_A ? BOOTCTL_SLOT_B : BOOTCTL_SLOT_A;
}

static uint32_t failed_bit(uint8_t slot) {
    return slot == BOOTCTL_SLOT_B ? BOOTINFO_SESSION_FAILED_B : BOOTINFO_SESSION_FAILED_A;
}

// The session word for starting `slot` (flags: BOOTINFO_TRIAL or not), after session `s`
static uint32_t start_word(uint32_t s, uint8_t slot, uint32_t flags) {
    const bool same = (s & BOOTINFO_SESSION_STARTED) &&
                      ((s & BOOTINFO_SESSION_LAST_B) != 0) == (slot == BOOTCTL_SLOT_B);
    uint32_t w = BOOTINFO_SESSION_MAGIC | BOOTINFO_SESSION_STARTED |
                 (s & (BOOTINFO_SESSION_FAILED_A | BOOTINFO_SESSION_FAILED_B));
    w |= slot == BOOTCTL_SLOT_B ? BOOTINFO_SESSION_LAST_B : 0;
    w |= (flags & BOOTINFO_TRIAL) ? BOOTINFO_SESSION_LAST_TRIAL : 0;
    w |= same ? (s & BOOTINFO_SESSION_CRASHES) : 0;    // Crashes in a row count per slot
    return w;
}

boot_decision_t boot_decide(const boot_inputs_t *in) {
    boot_decision_t d = {.slots = {BOOTCTL_NONE, BOOTCTL_NONE}};

    // Design D8: a power-on only when HAD_POR is set and our session marker isn't. HAD_POR alone
    // stays set after warm reboots (phase 0), and a brown-out (HAD_BOR) or RUN reset doesn't count:
    // the console may still be running then.
    d.power_on = in->had_por && !in->marker_set;

    // This session so far; a power-on starts a new one
    uint32_t s = in->session;
    if (d.power_on || (s & BOOTINFO_SESSION_MAGIC_MASK) != BOOTINFO_SESSION_MAGIC) {
        s = 0;
    }
    // How the slot started last ended: a reboot the firmware meant, or a crash (D12)
    if (s & BOOTINFO_SESSION_STARTED) {
        const uint8_t last = (s & BOOTINFO_SESSION_LAST_B) ? BOOTCTL_SLOT_B : BOOTCTL_SLOT_A;
        if (in->clean_reboot) {
            s &= ~BOOTINFO_SESSION_CRASHES;
        } else {
            if ((s & BOOTINFO_SESSION_CRASHES) < BOOTINFO_SESSION_CRASHES) {
                s++;
            }
            if (s & BOOTINFO_SESSION_LAST_TRIAL) {
                s |= failed_bit(last);          // A trial that crashes has failed
            }
        }
    }

    const bootctl_t *b = in->bootctl;
    d.bootctl_default = (b == NULL);
    const bootctl_t def = BOOTCTL_DEFAULT;
    if (!b) {
        b = &def;
    }
    uint8_t active = b->active_slot;
    if (active != BOOTCTL_SLOT_A && active != BOOTCTL_SLOT_B) {
        active = BOOTCTL_SLOT_A;
    }
    const uint8_t pending = b->pending_slot;
    const bool trial = (pending == BOOTCTL_SLOT_A || pending == BOOTCTL_SLOT_B) && pending != active &&
                       b->slot_state[pending] == BOOTCTL_STATE_TRIAL && !(s & failed_bit(pending));
    const bool last_was_active = (s & BOOTINFO_SESSION_STARTED) &&
                                 ((s & BOOTINFO_SESSION_LAST_B) != 0) == (active == BOOTCTL_SLOT_B);
    const bool crash_loop = last_was_active && (s & BOOTINFO_SESSION_CRASHES) >= BOOTINFO_CRASH_LOOP &&
                            b->slot_state[other(active)] == BOOTCTL_STATE_KEPT;

    if (trial) {
        // A new firmware on trial; if its slot holds no valid image, the trial has failed
        d.slots[0] = pending;
        d.flags[0] = BOOTINFO_TRIAL;
        d.slots[1] = active;
        d.flags[1] = BOOTINFO_TRIAL_FAILED;
    } else if (crash_loop) {
        // The active firmware crashed BOOTINFO_CRASH_LOOP times in a row: the other, kept one
        // takes over (the owner's choice: for good)
        d.slots[0] = other(active);
        d.flags[0] = BOOTINFO_CRASH_FALLBACK;
        d.slots[1] = active;
    } else {
        d.slots[0] = active;
        // A trial of the other slot that failed in this session: the active firmware records it
        const uint8_t o = other(active);
        d.flags[0] = (s & failed_bit(o)) && pending == o ? BOOTINFO_TRIAL_FAILED : 0;
        // The other slot only if the active one holds no valid image, and never one that's empty
        // (perhaps half written), failed, or on trial
        const uint8_t st = b->slot_state[o];
        if (st == BOOTCTL_STATE_INSTALLED || st == BOOTCTL_STATE_KEPT) {
            d.slots[1] = o;
            d.flags[1] = d.flags[0];
        }
    }
    // Never an empty slot (phase 3 review): an empty active slot gives way to the second choice,
    // which then starts in its place (BOOTINFO_FALLBACK) and becomes the active one
    for (int i = 0; i < 2; i++) {
        if (d.slots[i] != BOOTCTL_NONE && b->slot_state[d.slots[i]] == BOOTCTL_STATE_EMPTY) {
            d.slots[i] = BOOTCTL_NONE;
        }
    }
    if (d.slots[0] == BOOTCTL_NONE && d.slots[1] != BOOTCTL_NONE) {
        d.slots[0] = d.slots[1];
        d.flags[0] = d.flags[1] | BOOTINFO_FALLBACK;
        d.slots[1] = BOOTCTL_NONE;
    }

    d.session[0] = start_word(s, d.slots[0], d.flags[0]);
    // Starting the second means the first held no valid image: that ends a trial of it too
    const uint32_t s1 = (d.flags[0] & BOOTINFO_TRIAL) ? (s | failed_bit(d.slots[0])) : s;
    if (d.slots[1] != BOOTCTL_NONE) {
        d.session[1] = start_word(s1, d.slots[1], d.flags[1]);
    }
    return d;
}
