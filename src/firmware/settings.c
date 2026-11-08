/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "settings.h"

const settings_t SETTINGS_DEFAULT = {
    .ssid = "",
    .password = SETTINGS_DEFAULT_PASSWORD,
    .country = "XX",
    .channel = SETTINGS_CHANNEL_DEFAULT,
    .tx_power = SETTINGS_TX_MEDIUM,
    .auto_off_minutes = 10,
    .led = 1,
};

const char SETTINGS_COUNTRIES[] =
    "XX"
    "AU" "AT" "BE" "BR" "CA" "CL" "CN" "CO" "CZ" "DK" "EE" "FI" "FR" "DE" "GR" "HK" "HU" "IS"
    "IN" "IL" "IT" "JP" "KE" "LV" "LI" "LT" "LU" "MY" "MT" "MX" "NL" "NZ" "NG" "NO" "PE" "PH"
    "PL" "PT" "SG" "SK" "SI" "ZA" "KR" "ES" "SE" "CH" "TW" "TH" "TR" "GB" "US";

// Length of a printable-ASCII string within `size` bytes, or -1 if it isn't one (a character
// outside 0x20-0x7E, or no terminating NUL)
static int printable_length(const char *s, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        if (s[i] == '\0') {
            return (int)i;
        }
        if (s[i] < 0x20 || s[i] > 0x7E) {
            return -1;
        }
    }
    return -1;
}

bool settings_ssid_ok(const char *ssid) {
    return printable_length(ssid, SETTINGS_SSID_MAX + 1) >= 0;
}

bool settings_password_ok(const char *password) {
    const int n = printable_length(password, SETTINGS_PASSWORD_MAX + 1);
    return n >= (int)SETTINGS_PASSWORD_MIN;
}

bool settings_country_ok(const char *country) {
    if (printable_length(country, 3) != 2) {
        return false;
    }
    for (const char *c = SETTINGS_COUNTRIES; *c; c += 2) {
        if (c[0] == country[0] && c[1] == country[1]) {
            return true;
        }
    }
    return false;
}

bool settings_channel_ok(unsigned channel) {
    return channel >= 1 && channel <= SETTINGS_CHANNEL_MAX;
}

bool settings_tx_power_ok(unsigned level) {
    return level < SETTINGS_TX_LEVELS;
}

bool settings_auto_off_ok(unsigned minutes) {
    return minutes == SETTINGS_AUTO_OFF_NEVER || minutes == 2 || minutes == 5 || minutes == 10 || minutes == 30;
}

bool settings_sanitize(settings_t *s) {
    bool ok = true;
    if (!settings_ssid_ok(s->ssid)) {
        memcpy(s->ssid, SETTINGS_DEFAULT.ssid, sizeof(s->ssid));
        ok = false;
    }
    if (!settings_password_ok(s->password)) {
        memcpy(s->password, SETTINGS_DEFAULT.password, sizeof(s->password));
        ok = false;
    }
    if (!settings_country_ok(s->country)) {
        memcpy(s->country, SETTINGS_DEFAULT.country, sizeof(s->country));
        ok = false;
    }
    if (!settings_channel_ok(s->channel)) {
        s->channel = SETTINGS_DEFAULT.channel;
        ok = false;
    }
    if (!settings_tx_power_ok(s->tx_power)) {
        s->tx_power = SETTINGS_DEFAULT.tx_power;
        ok = false;
    }
    if (!settings_auto_off_ok(s->auto_off_minutes)) {
        s->auto_off_minutes = SETTINGS_DEFAULT.auto_off_minutes;
        ok = false;
    }
    if (s->led > 1) {
        s->led = SETTINGS_DEFAULT.led;
        ok = false;
    }
    for (unsigned i = 0; i < sizeof(s->reserved); i++) {
        if (s->reserved[i]) {
            s->reserved[i] = 0;
            ok = false;
        }
    }
    return ok;
}

bool settings_default_password(const settings_t *s) {
    return strcmp(s->password, SETTINGS_DEFAULT_PASSWORD) == 0;
}
