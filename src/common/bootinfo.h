/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// What the loader tells the firmware, in POWMAN scratch registers (design D7, D8). POWMAN scratch
// survives watchdog and software reboots, and is cleared at power-on, brown-out and RUN (phase 0
// confirmed this for warm reboots). The SDK's pico_low_power uses scratch 6 and 7.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BOOTINFO_SCRATCH_MARKER     0           // The session marker (design D8)
#define BOOTINFO_SCRATCH_INFO       1           // The loader's decision, below
#define BOOTINFO_SCRATCH_CHIP_RESET 2           // POWMAN CHIP_RESET as the loader read it

#define BOOTINFO_SESSION_MARKER     0x50424F4Eu // "PBON": set by the loader at every boot

// The info word: a check byte on top, so a stale or random value isn't taken for real
#define BOOTINFO_MAGIC              0xB1000000u
#define BOOTINFO_MAGIC_MASK         0xFF000000u
#define BOOTINFO_POWER_ON           (1u << 0)   // Inject: a real power-on (HAD_POR and no marker)
#define BOOTINFO_SLOT_B             (1u << 1)   // The slot the loader chained (else slot A)
#define BOOTINFO_FALLBACK           (1u << 2)   // The first slot it tried held no valid image
#define BOOTINFO_BOOTCTL_DEFAULT    (1u << 3)   // No valid boot control record: defaults used

static inline bool bootinfo_valid(uint32_t info) {
    return (info & BOOTINFO_MAGIC_MASK) == BOOTINFO_MAGIC;
}
