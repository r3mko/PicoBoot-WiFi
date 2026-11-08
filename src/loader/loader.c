/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The loader (design D7). The boot ROM starts it as a normal flash image at offset 0. It decides
// whether this boot is a power-on (D8), picks a firmware slot from boot control, tells the firmware
// both in POWMAN scratch, and hands over with rom_chain_image(), which maps the slot to 0x10000000.
//
// It stays small and quick: no clock setup, no flash writes, no Wi-Fi. Every decision is in
// boot_decide() (src/common/bootdecide.c), which the host tests cover.

#include "pico/bootrom.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/powman.h"
#include "layout.h"
#include "records.h"
#include "bootctl.h"
#include "bootinfo.h"
#include "bootdecide.h"

// rom_chain_image() needs a work area of at least 3264 bytes (SDK 2.3.1); one sector is plenty
static uint8_t chain_workarea[4096] __attribute__((aligned(4)));

static const uint32_t slot_offset[2] = {LAYOUT_SLOT_A_OFFSET, LAYOUT_SLOT_B_OFFSET};

int main(void) {
    const uint32_t chip_reset = powman_hw->chip_reset;
    const bool marker_set = powman_hw->scratch[BOOTINFO_SCRATCH_MARKER] == BOOTINFO_SESSION_MARKER;

    // Boot control, read straight from flash (the loader runs untranslated at offset 0)
    const record_header_t *rec = record_find((const uint8_t *)(XIP_BASE + LAYOUT_BOOTCTL_OFFSET),
                                             BOOTCTL_MAGIC, BOOTCTL_VERSION, sizeof(bootctl_t), NULL);
    const boot_inputs_t inputs = {
        .had_por = (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) != 0,
        .marker_set = marker_set,
        .bootctl = rec ? record_payload(rec) : NULL,
    };
    const boot_decision_t d = boot_decide(&inputs);

    // From now on every reboot until the next power-on is a warm one
    powman_hw->scratch[BOOTINFO_SCRATCH_MARKER] = BOOTINFO_SESSION_MARKER;
    powman_hw->scratch[BOOTINFO_SCRATCH_CHIP_RESET] = chip_reset;

    for (int i = 0; i < 2; i++) {
        uint32_t info = BOOTINFO_MAGIC;
        info |= d.power_on ? BOOTINFO_POWER_ON : 0;
        info |= d.slots[i] == BOOTCTL_SLOT_B ? BOOTINFO_SLOT_B : 0;
        info |= i > 0 ? BOOTINFO_FALLBACK : 0;
        info |= d.bootctl_default ? BOOTINFO_BOOTCTL_DEFAULT : 0;
        powman_hw->scratch[BOOTINFO_SCRATCH_INFO] = info;

        // Doesn't return if the slot holds a valid image
        rom_chain_image(chain_workarea, sizeof(chain_workarea),
                        XIP_BASE + slot_offset[d.slots[i]], LAYOUT_SLOT_SIZE);
    }

    // Neither slot holds a valid image: wait in BOOTSEL for a USB install
    powman_hw->scratch[BOOTINFO_SCRATCH_INFO] = 0;
    reset_usb_boot(0, 0);
}
