/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Wi-Fi (design D20, D26, D40, D41): the settings in use, the access point, the trial of a Wi-Fi
// change, Tx power, the LED and auto-off. Call these only after the injection: they read and write
// flash.

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

// The LED once Wi-Fi is up (D26): what the injection did
typedef enum {
    WIFI_LED_STEADY,            // On while Wi-Fi is on (no injection, or no console)
    WIFI_LED_DONE,              // Three slow blinks, as PicoBoot-DX, then on
    WIFI_LED_STALLED,           // Fast blinks for as long as Wi-Fi is on
} wifi_led_t;

// Starts the access point and the servers (net_start()); returns false on failure
bool wifi_start(net_page_fn page, net_action_fn action, net_api_fn api, const net_upload_t *const *uploads,
                wifi_led_t led);

// The Tx power the chip reports after the last change, in quarter dBm; -1 if unknown
int wifi_tx_power_qdbm(void);

// Stores `next` (already checked) as the settings in use. A change to the SSID, password, channel
// or country goes on trial and restarts the access point about a second from now (*restart).
// Returns false if the flash write failed; then nothing changed.
bool wifi_change(const settings_t *next, bool *restart);

// From the main loop: restarts the access point when due, ends a trial once a client came through
// the restarted access point, applies the Tx power and the LED, and turns Wi-Fi off after the
// auto-off time without a client (D20). Returns false once Wi-Fi is off: then the chip is powered
// down, and nothing may call net_poll() any more.
bool wifi_poll(void);
