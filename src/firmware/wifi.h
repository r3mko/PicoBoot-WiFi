/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Wi-Fi (design D40, D41): the settings in use, the access point, and the trial of a Wi-Fi change.
// Call these only after the injection: they read and write flash.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "net.h"
#include "settings.h"

// Loads the settings and undoes a Wi-Fi change that no client confirmed (D41)
void wifi_load(void);

const settings_t *wifi_settings(void);
bool wifi_settings_stored(void);        // false: the settings area was empty, these are the defaults
bool wifi_reverted(void);               // wifi_load() undid an unconfirmed Wi-Fi change
const char *wifi_ssid(void);            // The one in use: the setting, or PicoBoot-XXXX (D30)
uint32_t wifi_country(void);            // For cyw43_arch_init_with_country()

// Starts the access point and the servers (net_start()); returns false on failure
bool wifi_start(net_page_fn page, net_action_fn action, net_api_fn api);

// Stores `next` (already checked) as the settings in use. A change to the SSID, password, channel
// or country goes on trial and restarts the access point about a second from now (*restart).
// Returns false if the flash write failed; then nothing changed.
bool wifi_change(const settings_t *next, bool *restart);

// From the main loop: restarts the access point when due, and ends a trial once a client came
// through the restarted access point
void wifi_poll(void);
