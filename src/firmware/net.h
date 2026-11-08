/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>

// Renders the page into buf and returns its length (net.c calls it per request)
typedef size_t (*net_page_fn)(char *buf, size_t size);

// Handles a POST to `path` (a button on the page); returns false if it isn't one
typedef bool (*net_action_fn)(const char *path);

// Starts the access point (WPA2-AES, design D30), the DHCP and DNS servers (every name answers
// with 192.168.4.1, so phones open the page by themselves) and the HTTP server. cyw43_arch_init()
// must have succeeded first. Returns false on failure.
bool net_start(const char *ssid, const char *password, net_page_fn page, net_action_fn action);

// One iteration of the main loop: lets the CYW43 driver and lwIP do their work
void net_poll(void);

// Connected clients, refreshed once a second by net_poll() (never asked of the CYW43 from inside
// an lwIP callback)
int net_client_count(void);
