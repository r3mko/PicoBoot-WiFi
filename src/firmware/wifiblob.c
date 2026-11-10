/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdbool.h>
#include <string.h>
#include "wifiblob.h"
#include "cyw43_fixed_firmware.h"
#include "layout.h"
#include "bootctl.h"
#include "pico.h"                       // First: the board sets the chip, which platform_defs.h reads
#include "hardware/regs/addressmap.h"

// Read by the CYW43 driver through CYW43_WIFI_FW_LEN, CYW43_CLM_LEN and fw_data
int cyw43_wifi_fw_len;
int cyw43_clm_len;
uintptr_t fw_data;

typedef struct {
    char magic[4];              // "W43B"
    uint32_t version;           // 1
    uint32_t fw_len;            // Firmware bytes
    uint32_t clm_len;           // CLM bytes, after the firmware padded to 512
    uint32_t data_len;          // Bytes after this header
    uint8_t reserved[12];
    uint8_t sha256[32];         // Of the data; tells blobs apart (design D14, phase 3)
} wifiblob_header_t;

_Static_assert(sizeof(wifiblob_header_t) == 64, "the header of tools/mkwifiblob.py");

static bool valid(const wifiblob_header_t *h) {
    const uint32_t clm_offset = (h->fw_len + 511u) / 512u * 512u;
    return memcmp(h->magic, "W43B", 4) == 0 && h->version == 1 &&
           h->fw_len > 0 && h->fw_len < LAYOUT_WIFI_SIZE &&
           h->data_len <= LAYOUT_WIFI_SIZE - sizeof(*h) &&
           // Written so it can't wrap: clm_offset + clm_len would let a huge clm_len pass
           clm_offset <= h->data_len && h->clm_len <= h->data_len - clm_offset;
}

int wifiblob_select(uint8_t slot) {
    static const uint32_t offset[2] = {LAYOUT_WIFI_A_OFFSET, LAYOUT_WIFI_B_OFFSET};
    const uint8_t order[2] = {slot == BOOTCTL_SLOT_B ? 1 : 0, slot == BOOTCTL_SLOT_B ? 0 : 1};

    for (int i = 0; i < 2; i++) {
        // Untranslated XIP: the loader maps only this firmware's slot at XIP_BASE
        const uintptr_t base = XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset[order[i]];
        const wifiblob_header_t *h = (const wifiblob_header_t *)base;
        if (valid(h)) {
            cyw43_wifi_fw_len = (int)h->fw_len;
            cyw43_clm_len = (int)h->clm_len;
            fw_data = base + sizeof(*h);
            return order[i];
        }
    }
    return -1;
}
