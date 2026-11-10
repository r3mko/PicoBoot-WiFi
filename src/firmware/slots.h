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
// crash-looped, so this one becomes the active one and the other is marked failed;
// BOOTINFO_FALLBACK while this isn't the active slot: the active one holds no valid image, so this
// one becomes the active one and the other is marked empty)
bool slots_at_start(bootctl_t *b, uint8_t running, uint32_t loader_flags);

// Where an update goes (D11): the other slot, and only from the active firmware. Not while this one
// is on trial (the other is its fallback), nor when it runs in place of the active one: the update
// would then go into the active slot, without a trial (phase 3 review).
typedef enum {
    SLOTS_UPDATE_OK,
    SLOTS_UPDATE_ON_TRIAL,
    SLOTS_UPDATE_NOT_ACTIVE,
} slots_update_t;

slots_update_t slots_update_target(const bootctl_t *b, uint8_t running, uint8_t *target);

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

// Back to the active slot from a trial: the trial is abandoned (it stays unkept, never a fallback).
// To a kept slot: it becomes the active one (a running firmware from a USB install counts as kept
// from then on). To any other slot with a firmware (installed over USB, on trial, unkept or failed):
// it goes on trial (D12).
// The caller checks first that the slot holds a firmware (SLOTS_NOT_BOOTABLE covers empty slots).
slots_switch_t slots_switch(bootctl_t *b, uint8_t running, uint8_t target);

// A new trial of `slot` is set up (an upload into it, or slots_switch() putting it on trial again):
// the session word (BOOTINFO_SCRATCH_SESSION) without its note that the slot's trial failed in this
// session, so the loader tries the new one before the next power-on (phase 3 console test)
uint32_t slots_session_new_trial(uint32_t session, uint8_t slot);
