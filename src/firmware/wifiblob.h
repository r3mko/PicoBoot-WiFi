/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdint.h>

// Points the CYW43 driver at the blob in the Wi-Fi area paired with `slot` (design D14), or at the
// other area if that one holds no valid image. Call before cyw43_arch_init(). Returns the area used
// (BOOTCTL_SLOT_A for Wi-Fi A, BOOTCTL_SLOT_B for Wi-Fi B), or -1 if neither holds a valid image.
// The image format is described in tools/mkwifiblob.py.
int wifiblob_select(uint8_t slot);
