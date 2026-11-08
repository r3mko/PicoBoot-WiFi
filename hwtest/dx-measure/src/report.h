/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include "bootlog.h"

typedef struct {
    boot_record_t boot;             // This boot's measurements
    uint64_t injection_end_us;      // Timer value when the injection ended (or was given up)
    uint64_t wifi_up_us;            // Timer value when the access point was up
    const char *ssid;
    const char *password;
} report_info_t;

// Keeps a copy of the info and sets up the temperature sensor
void report_init(const report_info_t *info);

// Renders the report page (HTML) into buf; returns its length
size_t report_render(char *buf, size_t size);

// Renders the result page of an upload test (HTML) into buf; returns its length
size_t report_render_sink(char *buf, size_t size, uint32_t bytes, uint64_t elapsed_us, bool complete);
