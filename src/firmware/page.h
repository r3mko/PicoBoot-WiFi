/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include "report.h"

typedef struct {
    const boot_report_t *report;
    const char *ssid;
    const char *password;
    int wifi_area;              // BOOTCTL_SLOT_A / _B (Wi-Fi A / B), or -1
    uint32_t wifi_up_ms;        // From the end of the injection (or start-up) to the AP being up
} page_info_t;

// Keeps the info and sets up the temperature sensor
void page_init(const page_info_t *info);

// The minimal report page of phase 1 (plan step 1.8; phase 2 brings the real web UI)
size_t page_render(char *buf, size_t size);
