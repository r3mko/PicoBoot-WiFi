/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The firmware's side of the trial and fallback (design D12): what it writes to boot control. Each
// function changes *b and returns true if the caller must store it. Plain C, for host tests.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bootctl.h"

// At start-up, after the injection: what the loader said (BOOTINFO_TRIAL_FAILED: the other slot's
// trial failed, so it's marked failed and no longer pending; BOOTINFO_CRASH_FALLBACK: the other slot
// crash-looped, so this one becomes the active one and the other is marked failed)
bool slots_at_start(bootctl_t *b, uint8_t running, uint32_t loader_flags);

// The firmware on trial proved itself (a complete power-on injection, then Wi-Fi up) or the user
// pressed Keep: it becomes the active one. The slot it replaces counts as kept from then on, as a
// firmware that ran (so a later crash loop can fall back to it).
bool slots_keep(bootctl_t *b, uint8_t running);

// The firmware page's "Boot slot X" (D13); the caller reboots if it returns SLOTS_SWITCHED
typedef enum {
    SLOTS_SWITCHED,         // Boot control changed: X becomes the active slot, or goes on trial
    SLOTS_NOTHING_TO_DO,    // X is the running slot already
    SLOTS_NOT_BOOTABLE,     // X is empty (perhaps half written)
} slots_switch_t;

// To a kept or installed slot: it becomes the active one (a trial running now is abandoned: it stays
// unkept and is never a fallback). To a slot on trial or failed: it goes on trial again (D12).
slots_switch_t slots_switch(bootctl_t *b, uint8_t running, uint8_t target);
