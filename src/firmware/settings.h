/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Settings (design D22, D40): Wi-Fi and user settings, stored as a record (records.h) in the
// settings area. A USB install blanks the area, which means the defaults (D19: the way back from
// a forgotten password).
//
// Plain C without SDK calls, for host tests.

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_MAGIC              0x54544553u     // "SETT"
#define SETTINGS_VERSION            1

#define SETTINGS_SSID_MAX           32              // 802.11
#define SETTINGS_PASSWORD_MIN       8               // A WPA2 passphrase
#define SETTINGS_PASSWORD_MAX       63
#define SETTINGS_DEFAULT_PASSWORD   "picoboot"      // Design D30, in the README

#define SETTINGS_CHANNEL_DEFAULT    6
#define SETTINGS_CHANNEL_MAX        11              // 12 and 13 aren't allowed everywhere (D40)

// Tx power levels (D40); step 2.6 maps them to dBm
enum {
    SETTINGS_TX_LOW,
    SETTINGS_TX_MEDIUM,
    SETTINGS_TX_HIGH,
    SETTINGS_TX_MAX,
    SETTINGS_TX_LEVELS
};

#define SETTINGS_AUTO_OFF_NEVER     0               // Wi-Fi stays on (D20)

typedef struct {
    char ssid[SETTINGS_SSID_MAX + 1];           // Empty: PicoBoot-XXXX from the chip ID (D30)
    char password[SETTINGS_PASSWORD_MAX + 1];
    char country[3];                            // A code from SETTINGS_COUNTRIES; "XX" is worldwide
    uint8_t channel;                            // 1 to SETTINGS_CHANNEL_MAX
    uint8_t tx_power;                           // SETTINGS_TX_*
    uint8_t auto_off_minutes;                   // 2, 5, 10 or 30, or SETTINGS_AUTO_OFF_NEVER (D20)
    uint8_t led;                                // 1: the LED shows the status (D26); 0: always off
    uint8_t reserved[24];                       // Zero: room for later settings in the same version
} settings_t;

_Static_assert(sizeof(settings_t) == 128, "settings_t is stored in flash: its size is part of the format");

extern const settings_t SETTINGS_DEFAULT;

// The countries the CYW43 firmware knows (cyw43_country.h), two letters each, "XX" first
extern const char SETTINGS_COUNTRIES[];

// Checks of single values, for the settings API: true if the value may be stored
bool settings_ssid_ok(const char *ssid);            // Printable ASCII, empty allowed (the default)
bool settings_password_ok(const char *password);    // Printable ASCII, 8 to 63 characters
bool settings_country_ok(const char *country);
bool settings_channel_ok(unsigned channel);
bool settings_tx_power_ok(unsigned level);
bool settings_auto_off_ok(unsigned minutes);

// Puts the default in place of every value that isn't allowed, and zeroes the reserved bytes.
// Returns true if nothing had to change, so the settings were valid.
bool settings_sanitize(settings_t *s);

// The banner until the password is changed (D19)
bool settings_default_password(const settings_t *s);
