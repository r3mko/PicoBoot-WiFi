/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Settings (design D22, D40, D41): Wi-Fi and user settings, stored as a record (records.h) in the
// settings area. A USB install blanks the area, which means the defaults (D19: the way back from
// a forgotten password).
//
// Plain C without SDK calls, for host tests.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "json.h"

#define SETTINGS_MAGIC              0x54544553u     // "SETT"
#define SETTINGS_VERSION            2               // 2: settings_record_t with the D41 trial

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

// What the settings area holds: the settings, and during a D41 trial the ones to go back to
typedef struct {
    settings_t current;                         // In use
    settings_t previous;                        // While on trial: the last confirmed settings
    uint8_t trial;                              // 1: current has Wi-Fi changes no client has confirmed
    uint8_t reserved[15];
} settings_record_t;

_Static_assert(sizeof(settings_record_t) == 272, "settings_record_t is stored in flash");

// The record when the area is empty
extern const settings_record_t SETTINGS_RECORD_DEFAULT;

// Tx power level names, as in the API
extern const char *const SETTINGS_TX_NAMES[SETTINGS_TX_LEVELS];

// The allowed auto-off values, as the API lists them
#define SETTINGS_AUTO_OFF_CHOICES   5
extern const uint8_t SETTINGS_AUTO_OFF_MINUTES[SETTINGS_AUTO_OFF_CHOICES];

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

// --- The D41 trial: pure functions on the record; the caller stores it when they return true ------

// The SSID, password, channel or country differ: the access point has to restart
bool settings_wifi_differs(const settings_t *a, const settings_t *b);

// At start-up: a Wi-Fi change no client confirmed is undone (the SSID, password, channel and
// country go back; the other settings stay)
bool settings_record_start(settings_record_t *r);

// A change to `next` (already checked). Wi-Fi changes go on trial, keeping the last confirmed
// settings to go back to. Returns true if the record changed; *restart tells if the AP restarts.
bool settings_record_change(settings_record_t *r, const settings_t *next, bool *restart);

// A client came through the restarted access point: the trial ends, the change is kept
bool settings_record_confirm(settings_record_t *r);

// Puts the default in place of every value that isn't allowed, in both settings and the trial flag;
// returns true if nothing had to change
bool settings_record_sanitize(settings_record_t *r);

// --- POST /api/settings (docs/api.md) ------------------------------------------------------------

typedef enum {
    SETTINGS_SET_OK,
    SETTINGS_SET_UNKNOWN_FIELD,
    SETTINGS_SET_BAD_VALUE,
} settings_set_t;

// Sets one member of the body on *s. On SETTINGS_SET_BAD_VALUE, *message says what is allowed.
settings_set_t settings_set_member(settings_t *s, const char *key, const json_value_t *v, const char **message);
