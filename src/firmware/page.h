/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "report.h"

typedef struct {
    const boot_report_t *report;
    const char *ssid;
    const char *password;
    int wifi_area;              // BOOTCTL_SLOT_A / _B (Wi-Fi A / B), or -1
    uint32_t wifi_up_ms;        // From the end of the injection (or start-up) to the AP being up
    uint32_t cyw43_init_ms;     // ...of which cyw43_arch_init() (loads the blob from flash)
    uint32_t ap_start_ms;       // ...and net_start()
    // Flash read path of the injection (the untranslated, uncached XIP window), measured at start-up
    uint32_t qmi_rcmd, qmi_rfmt, qmi_timing;
    uint32_t sys_khz;
    uint32_t dma_64k_us;        // DMA read of 64 KB of payload 1 through that window
} page_info_t;

// Keeps the info and sets up the temperature sensor
void page_init(const page_info_t *info);

// The minimal report page of phase 1 (plan step 1.8; phase 2 brings the real web UI)
size_t page_render(char *buf, size_t size);

// The page's buttons: the payload for the next power-ons, and the CS test mode (plan step 1.10)
bool page_action(const char *path);
