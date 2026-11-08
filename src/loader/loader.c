/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The loader (design D7). The boot ROM starts it as a normal flash image at offset 0; it picks a
// firmware slot and hands over with rom_chain_image(), which maps the slot to 0x10000000.
//
// Step 1.1 of the implementation plan: always slot A. Step 1.6 adds the power-on rule (D8),
// boot control and the slot choice.

#include "pico/bootrom.h"
#include "hardware/regs/addressmap.h"
#include "layout.h"

// rom_chain_image() needs a work area of at least 3264 bytes (SDK 2.3.1); one sector is plenty
static uint8_t chain_workarea[4096] __attribute__((aligned(4)));

int main(void) {
    rom_chain_image(chain_workarea, sizeof(chain_workarea),
                    XIP_BASE + LAYOUT_SLOT_A_OFFSET, LAYOUT_SLOT_SIZE);

    // Only returns if slot A holds no valid image: wait in BOOTSEL for a USB install
    reset_usb_boot(0, 0);
}
