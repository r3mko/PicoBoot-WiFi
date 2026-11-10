/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// POST /api/firmware (design D11, docs/update-format.md): an update file streamed into the slot that
// isn't running, through update.c, with the flash, SHA-256 and boot control of this firmware.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "net.h"

// The running slot (BOOTCTL_SLOT_A / _B) and the installed loader's version; false if there's no
// loader (the firmware installed on its own), which can't take updates
void updater_init(uint8_t running_slot, uint32_t loader_version, bool from_loader);

// The upload hooks for net_start()
extern const net_upload_t UPDATER_UPLOAD;
