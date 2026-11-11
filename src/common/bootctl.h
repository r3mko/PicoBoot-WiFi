/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Boot control (design D23): which firmware slot and which payload boot. Stored as a record
// (records.h) in the boot control area; the loader reads it, only the firmware writes it.
//
// From format 2 (D42) the format only grows at the end: readers take the fields they know from any
// newer record (record_find_any()), so a new format needs no new loader. The loader reads only the
// first BOOTCTL_LOADER_BYTES.

#pragma once
#include <stdint.h>

#define BOOTCTL_MAGIC           0x4C544342u     // "BCTL"
#define BOOTCTL_VERSION         2               // 2: payload names (D42)

#define BOOTCTL_SLOT_A          0
#define BOOTCTL_SLOT_B          1
#define BOOTCTL_NONE            0xFF

// slot_state (D12, D13). 0 is what a USB install leaves: the slot holds whatever it holds.
#define BOOTCTL_STATE_INSTALLED 0       // Not written over the air (slot A after a USB install)
#define BOOTCTL_STATE_EMPTY     1       // Being written, or its upload failed: never to be started
#define BOOTCTL_STATE_TRIAL     2       // A new firmware on trial: pending_slot names it
#define BOOTCTL_STATE_KEPT      3       // Proved itself, or kept by the user (D12)
#define BOOTCTL_STATE_FAILED    4       // Its trial failed

#define BOOTCTL_PAYLOAD_1       0
#define BOOTCTL_PAYLOAD_2       1
#define BOOTCTL_PAYLOAD_BUILTIN 2

#define BOOTCTL_FLAG_ORIGINAL_IPL_ONCE  (1u << 0)   // Phase 4: skip the injection once
#define BOOTCTL_FLAG_TEST_CS_PULLDOWN   (1u << 7)   // Test builds only: PicoBoot's CS pull-down
                                                    // instead of the D36 pull-up (plan step 1.10's
                                                    // glitch check); release builds ignore it

#define BOOTCTL_NAME_MAX        24              // Characters in a payload's name (D42)

typedef struct {
    // Format 1, and all the loader reads (BOOTCTL_LOADER_BYTES)
    uint8_t active_slot;        // BOOTCTL_SLOT_A or _B
    uint8_t pending_slot;       // The slot on trial (D12), or BOOTCTL_NONE
    uint8_t slot_state[2];      // BOOTCTL_STATE_*, per slot
    uint8_t active_payload;     // BOOTCTL_PAYLOAD_* (D15)
    uint8_t flags;              // BOOTCTL_FLAG_*
    uint8_t reserved[2];
    uint32_t payload1_fingerprint;  // Detects USB payload installs (D17)
    // Format 2 (D42)
    char payload_name[2][BOOTCTL_NAME_MAX + 1];    // Payload 1 and 2; printable ASCII, "" for none
    uint8_t reserved2[2];
} bootctl_t;

#define BOOTCTL_LOADER_BYTES    12
_Static_assert(sizeof(bootctl_t) == 64, "bootctl_t is stored in flash: its size is part of the format");

// What boot control means when the area holds no valid record: the state right after a USB install.
// Slot B counts as empty: the install doesn't touch it, so it may hold anything (an old or a test
// build), and is never started until an update has written it (phase 3 review).
static const bootctl_t BOOTCTL_DEFAULT = {
    .active_slot = BOOTCTL_SLOT_A,
    .pending_slot = BOOTCTL_NONE,
    .slot_state = {BOOTCTL_STATE_INSTALLED, BOOTCTL_STATE_EMPTY},
    .active_payload = BOOTCTL_PAYLOAD_1,
};
