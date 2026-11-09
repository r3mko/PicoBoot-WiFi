/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "settings.h"

#define DEFAULTS {                      \
    .ssid = "",                         \
    .password = SETTINGS_DEFAULT_PASSWORD, \
    .country = "XX",                    \
    .channel = SETTINGS_CHANNEL_DEFAULT, \
    .tx_power = SETTINGS_TX_MEDIUM,     \
    .auto_off_minutes = 10,             \
    .led = 1,                           \
}

const settings_t SETTINGS_DEFAULT = DEFAULTS;
const settings_record_t SETTINGS_RECORD_DEFAULT = {.current = DEFAULTS};

const char *const SETTINGS_TX_NAMES[SETTINGS_TX_LEVELS] = {"low", "medium", "high", "max"};

const uint8_t SETTINGS_AUTO_OFF_MINUTES[SETTINGS_AUTO_OFF_CHOICES] = {2, 5, 10, 30, SETTINGS_AUTO_OFF_NEVER};

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
    for (unsigned i = 0; i < SETTINGS_AUTO_OFF_CHOICES; i++) {
        if (minutes == SETTINGS_AUTO_OFF_MINUTES[i]) {
            return true;
        }
    }
    return false;
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

// --- The D41 trial --------------------------------------------------------------------------------

bool settings_wifi_differs(const settings_t *a, const settings_t *b) {
    return strcmp(a->ssid, b->ssid) != 0 || strcmp(a->password, b->password) != 0 ||
           strcmp(a->country, b->country) != 0 || a->channel != b->channel;
}

static void end_trial(settings_record_t *r) {
    memset(&r->previous, 0, sizeof(r->previous));
    r->trial = 0;
}

bool settings_record_start(settings_record_t *r) {
    if (!r->trial) {
        return false;
    }
    // Only the Wi-Fi settings go back; LED, Tx power and auto-off saved with them stay (D41)
    settings_t *c = &r->current;
    const settings_t *p = &r->previous;
    memcpy(c->ssid, p->ssid, sizeof(c->ssid));
    memcpy(c->password, p->password, sizeof(c->password));
    memcpy(c->country, p->country, sizeof(c->country));
    c->channel = p->channel;
    end_trial(r);
    return true;
}

bool settings_record_change(settings_record_t *r, const settings_t *next, bool *restart) {
    *restart = settings_wifi_differs(&r->current, next);
    if (!*restart && memcmp(&r->current, next, sizeof(*next)) == 0) {
        return false;
    }
    if (*restart && !r->trial) {
        r->previous = r->current;       // The confirmed settings; a second change on trial keeps them
        r->trial = 1;
    }
    r->current = *next;
    return true;
}

bool settings_record_confirm(settings_record_t *r) {
    if (!r->trial) {
        return false;
    }
    end_trial(r);
    return true;
}

bool settings_record_sanitize(settings_record_t *r) {
    bool ok = settings_sanitize(&r->current);
    if (r->trial > 1) {
        r->trial = 0;
        ok = false;
    }
    if (r->trial) {
        if (!settings_sanitize(&r->previous)) {
            ok = false;
        }
    } else {
        static const settings_t zero;
        if (memcmp(&r->previous, &zero, sizeof(zero)) != 0) {
            memset(&r->previous, 0, sizeof(r->previous));
            ok = false;
        }
    }
    for (unsigned i = 0; i < sizeof(r->reserved); i++) {
        if (r->reserved[i]) {
            r->reserved[i] = 0;
            ok = false;
        }
    }
    return ok;
}

// --- POST /api/settings ---------------------------------------------------------------------------

static void set_string(char *field, size_t size, const char *value) {
    memset(field, 0, size);
    memcpy(field, value, strlen(value));    // Checked to fit
}

settings_set_t settings_set_member(settings_t *s, const char *key, const json_value_t *v, const char **message) {
    const bool string = v->type == JSON_STRING && !v->too_long;
    const bool number = v->type == JSON_NUMBER && v->integer && v->number >= 0;
    if (strcmp(key, "ssid") == 0) {
        *message = "ssid must be 0 to 32 printable ASCII characters";
        if (!string || !settings_ssid_ok(v->string)) {
            return SETTINGS_SET_BAD_VALUE;
        }
        set_string(s->ssid, sizeof(s->ssid), v->string);
    } else if (strcmp(key, "password") == 0) {
        *message = "password must be 8 to 63 printable ASCII characters";
        if (!string || !settings_password_ok(v->string)) {
            return SETTINGS_SET_BAD_VALUE;
        }
        set_string(s->password, sizeof(s->password), v->string);
    } else if (strcmp(key, "country") == 0) {
        *message = "country must be one of choices.countries";
        if (!string || !settings_country_ok(v->string)) {
            return SETTINGS_SET_BAD_VALUE;
        }
        set_string(s->country, sizeof(s->country), v->string);
    } else if (strcmp(key, "channel") == 0) {
        *message = "channel must be 1 to 11";
        if (!number || !settings_channel_ok((unsigned)v->number)) {
            return SETTINGS_SET_BAD_VALUE;
        }
        s->channel = (uint8_t)v->number;
    } else if (strcmp(key, "tx_power") == 0) {
        *message = "tx_power must be low, medium, high or max";
        unsigned level = SETTINGS_TX_LEVELS;
        for (unsigned i = 0; string && i < SETTINGS_TX_LEVELS; i++) {
            if (strcmp(v->string, SETTINGS_TX_NAMES[i]) == 0) {
                level = i;
            }
        }
        if (level == SETTINGS_TX_LEVELS) {
            return SETTINGS_SET_BAD_VALUE;
        }
        s->tx_power = (uint8_t)level;
    } else if (strcmp(key, "auto_off_minutes") == 0) {
        *message = "auto_off_minutes must be 2, 5, 10, 30 or 0 (always on)";
        if (!number || !settings_auto_off_ok((unsigned)v->number)) {
            return SETTINGS_SET_BAD_VALUE;
        }
        s->auto_off_minutes = (uint8_t)v->number;
    } else if (strcmp(key, "led") == 0) {
        *message = "led must be true or false";
        if (v->type != JSON_BOOL) {
            return SETTINGS_SET_BAD_VALUE;
        }
        s->led = v->boolean;
    } else {
        return SETTINGS_SET_UNKNOWN_FIELD;
    }
    return SETTINGS_SET_OK;
}
