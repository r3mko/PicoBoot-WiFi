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

// The backup of the running firmware (D42): an update file, as tools/mkupdate.py makes it, of the
// image in the running slot and the Wi-Fi blob in `wifi_area` (BOOTCTL_SLOT_A / _B: the one it runs
// with), the header made here. Uses the SHA-256 hardware: not while an upload runs (net.c makes
// sure). False if that Wi-Fi area holds no blob.
bool updater_backup(int wifi_area, net_file_t *file);
