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

// Tx power per level (D40, the owner's choice in plan step 2.6), in quarter dBm: 5, 10 and 15 dBm;
// max asks for more than the chip allows, so its own limit applies
static const uint8_t TX_QDBM[SETTINGS_TX_LEVELS] = {5 * 4, 10 * 4, 15 * 4, 127};

static settings_record_t record;
static bool stored, reverted;
static char ssid[SETTINGS_SSID_MAX + 1];
static uint64_t restart_at_us;              // 0: no restart due
static bool awaiting_client;                // On trial, and the AP restarted with the change
static uint32_t requests_at_restart;
static bool tx_power_due;                   // Apply the Tx power from the main loop
static int tx_qdbm = -1;
static wifi_led_t led_pattern;
static uint64_t led_start_us;
static int led_state = -1;                  // What the LED was last set to; -1: set it again
static uint64_t last_client_us;             // For auto-off (D20)
static bool off;

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

bool wifi_start(net_page_fn page, net_action_fn action, net_api_fn api, const net_upload_t *const *uploads,
                const net_download_t *download, wifi_led_t led) {
    const net_ap_t ap = ap_settings();
    if (!net_start(&ap, page, action, api, uploads, download)) {
        return false;
    }
    led_pattern = led;
    led_start_us = last_client_us = time_us_64();
    tx_power_due = true;
    return true;
}

int wifi_tx_power_qdbm(void) {
    return tx_qdbm;
}

// The "qtxpower" iovar, as MicroPython's WLAN.config(txpower=...) sets it. Only from the main loop:
// the CYW43 mustn't be asked from inside an lwIP callback.
static void apply_tx_power(void) {
    uint8_t buf[9 + 4] = "qtxpower";        // The name with its NUL, then the value, little-endian
    const uint32_t q = TX_QDBM[record.current.tx_power];
    memcpy(buf + 9, &q, sizeof(q));
    cyw43_ioctl(&cyw43_state, CYW43_IOCTL_SET_VAR, sizeof(buf), buf, CYW43_ITF_AP);

    uint8_t back[9 + 4] = "qtxpower";       // Read back: the answer replaces the name
    tx_qdbm = -1;
    if (cyw43_ioctl(&cyw43_state, CYW43_IOCTL_GET_VAR, sizeof(back), back, CYW43_ITF_AP) == 0) {
        uint32_t v;
        memcpy(&v, back, sizeof(v));
        tx_qdbm = (int)(v & 0xFFu);         // The top bit is the chip's override flag
    }
}

// D26: dark while there's no Wi-Fi; after the injection three slow blinks (complete) or fast blinks
// for as long as Wi-Fi is on (stalled); otherwise on. The LED setting turns it off altogether.
static void update_led(void) {
    const uint64_t t = time_us_64() - led_start_us;
    bool on = true;
    if (!record.current.led) {
        on = false;
    } else if (led_pattern == WIFI_LED_STALLED) {
        on = (t / 100000) % 2 == 0;
    } else if (led_pattern == WIFI_LED_DONE && t < 1500000) {
        on = (t / 250000) % 2 == 1;         // Off, on, off, on, off, on: 250 ms each
    }
    if ((int)on != led_state) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
        led_state = on;
    }
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
    tx_power_due = true;                        // From the main loop; the LED and auto-off follow by themselves
    return true;
}

bool wifi_poll(void) {
    if (off) {
        return false;
    }
    const uint64_t now = time_us_64();
    if (restart_at_us && now >= restart_at_us) {
        restart_at_us = 0;
        const net_ap_t ap = ap_settings();
        const bool restarted = net_restart(&ap);
        requests_at_restart = net_requests();
        // If the AP didn't go down, it still runs the old settings and its clients mustn't confirm
        // the change: it stays on trial and is undone at the next start (phase 2 review finding 2)
        awaiting_client = record.trial && restarted;
        tx_power_due = true;                    // The chip was set up again
        led_state = -1;
    }
    confirm_if_seen();
    if (tx_power_due) {
        tx_power_due = false;
        apply_tx_power();
    }
    update_led();

    // Auto-off (D20): the countdown starts when the last client leaves (or at start-up)
    if (net_client_count() > 0 || restart_at_us) {
        last_client_us = now;
    }
    const unsigned minutes = record.current.auto_off_minutes;
    if (minutes != SETTINGS_AUTO_OFF_NEVER && now - last_client_us >= (uint64_t)minutes * 60 * 1000000) {
        cyw43_arch_deinit();                    // Powers the chip down; the LED goes dark with it
        off = true;
        return false;
    }
    return true;
}
