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
