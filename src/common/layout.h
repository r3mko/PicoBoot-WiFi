/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The flash layout of PicoBoot-WiFi (design D6, D37, D39). This is the only place it is written
// down: CMake reads it for the linker checks, and the tools and tests/check_layout.py parse it.
//
// Offsets are from the start of flash. Keep every value a plain hex number without a suffix,
// so CMake, the linker and the Python tools can read the same lines.

#pragma once

#define LAYOUT_FLASH_SIZE           0x00400000  // 4 MB W25Q32RV on the Pico 2 W

#define LAYOUT_LOADER_OFFSET        0x00000000  // The loader (D7); only a USB install writes it
#define LAYOUT_LOADER_SIZE          0x00002000

#define LAYOUT_BOOTLOG_OFFSET       0x00002000  // Boot log: the last boot reports, two sectors (D39)
#define LAYOUT_BOOTLOG_SIZE         0x00002000

#define LAYOUT_BOOTCTL_OFFSET       0x00004000  // Boot control: two sectors, one record each (D23)
#define LAYOUT_BOOTCTL_SIZE         0x00002000

#define LAYOUT_SETTINGS_OFFSET      0x00006000  // Settings: two sectors, one record each (D23)
#define LAYOUT_SETTINGS_SIZE        0x00002000

#define LAYOUT_WIFI_A_OFFSET        0x00008000  // CYW43 blob paired with slot A (D14)
#define LAYOUT_WIFI_B_OFFSET        0x00044000  // CYW43 blob paired with slot B
#define LAYOUT_WIFI_SIZE            0x0003C000

#define LAYOUT_PAYLOAD1_OFFSET      0x00080000  // Payload 1: where PicoBoot payload UF2s land (C3)
#define LAYOUT_PAYLOAD1_SIZE        0x00180000

#define LAYOUT_SLOT_A_OFFSET        0x00200000  // Firmware slot A
#define LAYOUT_SLOT_B_OFFSET        0x00240000  // Firmware slot B
#define LAYOUT_SLOT_SIZE            0x00040000

#define LAYOUT_PAYLOAD2_OFFSET      0x00280000  // Payload 2 (D15), one sector short of the end (D37)
#define LAYOUT_PAYLOAD2_SIZE        0x0017F000

#define LAYOUT_RESERVED_OFFSET      0x003FF000  // Kept free: stock RP2350 UF2s write picotool's
#define LAYOUT_RESERVED_SIZE        0x00001000  // RP2350-E10 block into this last page (D37)

#define LAYOUT_SECTOR_SIZE          0x00001000  // Flash erase unit
