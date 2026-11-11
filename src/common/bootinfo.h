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
#define BOOTINFO_SCRATCH_SESSION    3           // The loader's memory of this power-on session (D12)
#define BOOTINFO_SCRATCH_REBOOT     4           // The firmware's word on why it rebooted, below

#define BOOTINFO_SESSION_MARKER     0x50424F4Eu // "PBON": set by the loader at every boot

// Loader versions (docs/update-format.md). The loader's version counts up when its side of boot
// info or boot control changes in a way older loaders don't handle; loaders that don't report one
// are version 1. An update file names the lowest loader version its firmware works with, and the
// Pico refuses it if the installed loader is older: the loader only changes with a USB install.
#define BOOTINFO_LOADER_VERSION     3           // This loader (2: trial, fallback and crash loop, D12;
                                                // 3: reads boot control of any format from 2, D42)
#define BOOTINFO_LOADER_NEEDED      3           // The lowest loader this firmware works with

// Written by the firmware just before every reboot it means (the UI, an update). A reset without it
// is a crash of the slot started last: the watchdog, or the fault handler (D12).
#define BOOTINFO_CLEAN_REBOOT       0x50424352u // "PBCR"

// The info word: a check byte on top, so a stale or random value isn't taken for real
#define BOOTINFO_MAGIC              0xB1000000u
#define BOOTINFO_MAGIC_MASK         0xFF000000u
#define BOOTINFO_POWER_ON           (1u << 0)   // Inject: a real power-on (HAD_POR and no marker)
#define BOOTINFO_SLOT_B             (1u << 1)   // The slot the loader chained (else slot A)
#define BOOTINFO_FALLBACK           (1u << 2)   // The first slot it tried held no valid image
#define BOOTINFO_BOOTCTL_DEFAULT    (1u << 3)   // No valid boot control record: defaults used
#define BOOTINFO_TRIAL              (1u << 4)   // This boot starts the pending slot on trial (D12)
#define BOOTINFO_TRIAL_FAILED       (1u << 5)   // The other slot's trial failed this session: record it
#define BOOTINFO_CRASH_FALLBACK     (1u << 6)   // The other slot crashed 3 times this session: this one
                                                // takes over (D12, the owner's choice: for good)
#define BOOTINFO_LOADER_SHIFT       16          // Bits 16-23: the loader's version; 0 from loaders
#define BOOTINFO_LOADER_MASK        (0xFFu << BOOTINFO_LOADER_SHIFT)    // before version 2

// The session word in BOOTINFO_SCRATCH_SESSION: what the loader started last and how it went. POWMAN
// scratch is cleared at power-on, so each console session starts afresh.
#define BOOTINFO_SESSION_MAGIC      0x5E000000u
#define BOOTINFO_SESSION_MAGIC_MASK 0xFF000000u
#define BOOTINFO_SESSION_CRASHES    0x000000FFu // Crashes in a row of the slot started last
#define BOOTINFO_SESSION_STARTED    (1u << 8)   // A slot was started in this session
#define BOOTINFO_SESSION_LAST_B     (1u << 9)   // ...and it was slot B (else A)
#define BOOTINFO_SESSION_LAST_TRIAL (1u << 10)  // ...on trial
#define BOOTINFO_SESSION_FAILED_A   (1u << 11)  // Slot A's trial failed in this session
#define BOOTINFO_SESSION_FAILED_B   (1u << 12)  // Slot B's trial failed in this session; the
                                                // firmware clears it when it sets up a new trial
                                                // of that slot (an upload, or booting it again)

#define BOOTINFO_CRASH_LOOP         3           // Crashes in one session before the fallback (the owner's choice)

static inline bool bootinfo_valid(uint32_t info) {
    return (info & BOOTINFO_MAGIC_MASK) == BOOTINFO_MAGIC;
}
