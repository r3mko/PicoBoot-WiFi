/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>

// Starts the access point (WPA2), the DHCP and DNS servers and the HTTP server.
// cyw43_arch_init() must have succeeded first. Returns false on failure.
bool net_start(const char *ssid, const char *password);

// One iteration of the main loop: lets the CYW43 driver and lwIP do their work
void net_poll(void);

// Connected clients, refreshed once a second by net_poll(). The report reads this instead of
// asking the CYW43 itself, because it runs inside an lwIP callback, inside cyw43_arch_poll().
int net_client_count(void);
