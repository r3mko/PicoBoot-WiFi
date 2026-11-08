/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>
#include "wifi.h"
#include "storage.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
#include "pico/unique_id.h"

#define RESTART_DELAY_US (1000 * 1000)      // Time for the answer to reach the client (D41)

static settings_record_t record;
static bool stored, reverted;
static char ssid[SETTINGS_SSID_MAX + 1];
static uint64_t restart_at_us;              // 0: no restart due
static bool awaiting_client;                // On trial, and the AP restarted with the change
static uint32_t requests_at_restart;

static void update_ssid(void) {
    if (record.current.ssid[0]) {
        memcpy(ssid, record.current.ssid, sizeof(ssid));
        return;
    }
    // The last four hex digits of the chip's unique ID (design D30)
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    snprintf(ssid, sizeof(ssid), "PicoBoot-%02X%02X",
             id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2], id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1]);
}

static net_ap_t ap_settings(void) {
    const settings_t *s = &record.current;
    return (net_ap_t){ssid, s->password, s->channel, CYW43_COUNTRY(s->country[0], s->country[1], 0)};
}

void wifi_load(void) {
    stored = storage_settings_load(&record);
    if (settings_record_start(&record)) {
        // No client confirmed the last Wi-Fi change before this start: back to the settings before it.
        // If this write fails, the next start tries again.
        reverted = true;
        storage_settings_store(&record);
    }
    update_ssid();
}

const settings_t *wifi_settings(void) {
    return &record.current;
}

bool wifi_settings_stored(void) {
    return stored;
}

bool wifi_reverted(void) {
    return reverted;
}

const char *wifi_ssid(void) {
    return ssid;
}

uint32_t wifi_country(void) {
    return CYW43_COUNTRY(record.current.country[0], record.current.country[1], 0);
}

bool wifi_start(net_page_fn page, net_action_fn action, net_api_fn api) {
    const net_ap_t ap = ap_settings();
    return net_start(&ap, page, action, api);
}

// A request reached the web server through the restarted access point: the change works
static void confirm_if_seen(void) {
    if (awaiting_client && net_requests() != requests_at_restart) {
        awaiting_client = false;
        if (settings_record_confirm(&record)) {
            storage_settings_store(&record);    // If this fails, the change is undone at the next start
        }
    }
}

bool wifi_change(const settings_t *next, bool *restart) {
    // This request itself may be the first through a restarted AP: end that trial before a new one
    confirm_if_seen();
    settings_record_t r = record;
    if (!settings_record_change(&r, next, restart)) {
        return true;                            // Nothing changed
    }
    if (!storage_settings_store(&r)) {
        *restart = false;
        return false;
    }
    record = r;
    stored = true;
    update_ssid();                              // Also for the answer: the name to join next
    if (*restart) {
        restart_at_us = time_us_64() + RESTART_DELAY_US;
        awaiting_client = false;                // Until the AP runs the new settings
    }
    // Tx power, LED and auto-off take effect in plan step 2.6
    return true;
}

void wifi_poll(void) {
    if (restart_at_us && time_us_64() >= restart_at_us) {
        restart_at_us = 0;
        const net_ap_t ap = ap_settings();
        net_restart(&ap);
        requests_at_restart = net_requests();
        awaiting_client = record.trial;
    }
    confirm_if_seen();
}
