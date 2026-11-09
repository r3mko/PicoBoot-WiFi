/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/settings.c (design D22, D40, D41) and for settings as a record (D23).
// Built and run by tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "records.h"
#include "settings.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- Simulated NOR flash, as in test_records.c ------------------------------------------------

static uint8_t area[RECORD_AREA_SIZE];
static long budget = -1;            // Bytes the next program may still change; -1 = no power cut

static void fake_erase(void *ctx, uint32_t offset) {
    (void)ctx;
    memset(area + offset, 0xFF, RECORD_SECTOR_SIZE);
}

static void fake_program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    (void)ctx;
    for (size_t i = 0; i < length && budget != 0; i++) {
        if (budget > 0) {
            budget--;
        }
        area[offset + i] &= data[i];
    }
}

static const record_flash_t flash = {fake_erase, fake_program, NULL};

static bool store(const settings_record_t *r) {
    return record_write(area, &flash, SETTINGS_MAGIC, SETTINGS_VERSION, r, sizeof(*r));
}

// What the firmware finds after a reboot
static const settings_record_t *load(void) {
    const record_header_t *rec = record_find(area, SETTINGS_MAGIC, SETTINGS_VERSION, sizeof(settings_record_t), NULL);
    return rec ? record_payload(rec) : NULL;
}

// --- Tests ---------------------------------------------------------------------------------------

static void test_defaults(void) {
    settings_t s = SETTINGS_DEFAULT;
    CHECK(settings_sanitize(&s));
    CHECK(memcmp(&s, &SETTINGS_DEFAULT, sizeof(s)) == 0);
    CHECK(s.ssid[0] == '\0');                       // PicoBoot-XXXX (D30)
    CHECK(strcmp(s.password, "picoboot") == 0);     // D30
    CHECK(strcmp(s.country, "XX") == 0);            // Worldwide
    CHECK(s.channel == 6);                          // D40
    CHECK(s.tx_power == SETTINGS_TX_MEDIUM);        // D40
    CHECK(s.auto_off_minutes == 10);                // D20
    CHECK(s.led == 1);                              // D26
    CHECK(settings_default_password(&s));           // D19: the banner shows
}

static void test_ssid(void) {
    char ssid[SETTINGS_SSID_MAX + 2];
    CHECK(settings_ssid_ok(""));
    CHECK(settings_ssid_ok("My PicoBoot ~!"));
    memset(ssid, 'a', sizeof(ssid));
    ssid[SETTINGS_SSID_MAX] = '\0';
    CHECK(settings_ssid_ok(ssid));                  // 32 characters
    ssid[SETTINGS_SSID_MAX] = 'a';
    ssid[SETTINGS_SSID_MAX + 1] = '\0';
    CHECK(!settings_ssid_ok(ssid));                 // 33 characters
    CHECK(!settings_ssid_ok("tab\there"));
    CHECK(!settings_ssid_ok("del\x7f"));
    CHECK(!settings_ssid_ok("caf\xc3\xa9"));        // Not ASCII
}

static void test_password(void) {
    char pw[SETTINGS_PASSWORD_MAX + 2];
    CHECK(!settings_password_ok(""));
    CHECK(!settings_password_ok("1234567"));        // 7: below the WPA2 minimum
    CHECK(settings_password_ok("12345678"));
    CHECK(settings_password_ok("with spaces and !@#"));
    memset(pw, 'p', sizeof(pw));
    pw[SETTINGS_PASSWORD_MAX] = '\0';
    CHECK(settings_password_ok(pw));                // 63
    pw[SETTINGS_PASSWORD_MAX] = 'p';
    pw[SETTINGS_PASSWORD_MAX + 1] = '\0';
    CHECK(!settings_password_ok(pw));               // 64 would be a raw key, not a passphrase
    CHECK(!settings_password_ok("newline\npassword"));

    settings_t s = SETTINGS_DEFAULT;
    strcpy(s.password, "something else");
    CHECK(!settings_default_password(&s));          // D19: the banner goes
}

static void test_country(void) {
    CHECK(settings_country_ok("XX"));
    CHECK(settings_country_ok("NL"));
    CHECK(settings_country_ok("US"));
    CHECK(settings_country_ok("GB"));
    CHECK(!settings_country_ok("nl"));
    CHECK(!settings_country_ok("QQ"));
    CHECK(!settings_country_ok("N"));
    CHECK(!settings_country_ok("NLD"));
    CHECK(!settings_country_ok(""));
    CHECK(strlen(SETTINGS_COUNTRIES) % 2 == 0 && strncmp(SETTINGS_COUNTRIES, "XX", 2) == 0);
}

static void test_numbers(void) {
    CHECK(!settings_channel_ok(0));
    CHECK(settings_channel_ok(1) && settings_channel_ok(6) && settings_channel_ok(11));
    CHECK(!settings_channel_ok(12) && !settings_channel_ok(13) && !settings_channel_ok(14));
    for (unsigned level = 0; level < SETTINGS_TX_LEVELS; level++) {
        CHECK(settings_tx_power_ok(level));
    }
    CHECK(!settings_tx_power_ok(SETTINGS_TX_LEVELS));
    CHECK(settings_auto_off_ok(SETTINGS_AUTO_OFF_NEVER));
    CHECK(settings_auto_off_ok(2) && settings_auto_off_ok(5) && settings_auto_off_ok(10) && settings_auto_off_ok(30));
    CHECK(!settings_auto_off_ok(1) && !settings_auto_off_ok(15) && !settings_auto_off_ok(255));
}

static void test_sanitize_replaces_only_bad_values(void) {
    settings_t s = SETTINGS_DEFAULT;
    strcpy(s.ssid, "Kitchen");
    strcpy(s.password, "short");                    // Not allowed
    strcpy(s.country, "NL");
    s.channel = 13;                                 // Not allowed
    s.tx_power = SETTINGS_TX_LOW;
    s.auto_off_minutes = 7;                         // Not allowed
    s.led = 0;
    s.reserved[3] = 1;
    CHECK(!settings_sanitize(&s));
    CHECK(strcmp(s.ssid, "Kitchen") == 0);
    CHECK(strcmp(s.password, SETTINGS_DEFAULT_PASSWORD) == 0);
    CHECK(strcmp(s.country, "NL") == 0);
    CHECK(s.channel == SETTINGS_CHANNEL_DEFAULT);
    CHECK(s.tx_power == SETTINGS_TX_LOW);
    CHECK(s.auto_off_minutes == 10);
    CHECK(s.led == 0);
    CHECK(s.reserved[3] == 0);
    CHECK(settings_sanitize(&s));                   // Now valid

    // Strings without their NUL (all 0xFF, as erased flash) fall back too
    memset(&s, 0xFF, sizeof(s));
    CHECK(!settings_sanitize(&s));
    CHECK(memcmp(&s, &SETTINGS_DEFAULT, sizeof(s)) == 0);
}

static void test_settings_survive_a_reboot(void) {
    memset(area, 0xFF, sizeof(area));
    CHECK(load() == NULL);                          // After a USB install: the defaults

    settings_record_t r = SETTINGS_RECORD_DEFAULT;
    CHECK(settings_record_sanitize(&r));            // The empty-area record is valid
    strcpy(r.current.ssid, "GameCube");
    strcpy(r.current.password, "a new password");
    r.current.channel = 11;
    CHECK(store(&r));
    const settings_record_t *found = load();
    CHECK(found && memcmp(found, &r, sizeof(r)) == 0);

    // A power cut during the next change keeps the previous settings
    settings_record_t t = r;
    t.current.channel = 1;
    budget = 40;
    CHECK(!store(&t));
    budget = -1;
    found = load();
    CHECK(found && found->current.channel == 11 && strcmp(found->current.password, "a new password") == 0);

    // ...and the change goes through when tried again
    CHECK(store(&t));
    found = load();
    CHECK(found && found->current.channel == 1);
}

// --- D41: the trial of a Wi-Fi change -------------------------------------------------------------

static void test_trial_kept_once_a_client_comes(void) {
    settings_record_t r = SETTINGS_RECORD_DEFAULT;
    settings_t next = r.current;
    bool restart;
    strcpy(next.password, "typo in it");
    CHECK(settings_record_change(&r, &next, &restart) && restart);
    CHECK(r.trial == 1);
    CHECK(strcmp(r.current.password, "typo in it") == 0);
    CHECK(strcmp(r.previous.password, SETTINGS_DEFAULT_PASSWORD) == 0);
    CHECK(settings_record_sanitize(&r));            // A record on trial is valid

    CHECK(settings_record_confirm(&r));
    CHECK(r.trial == 0 && strcmp(r.current.password, "typo in it") == 0);
    static const settings_t zero;
    CHECK(memcmp(&r.previous, &zero, sizeof(zero)) == 0);
    CHECK(!settings_record_confirm(&r));            // Nothing more to confirm
    CHECK(!settings_record_start(&r));              // ...and nothing to undo at the next start
}

static void test_trial_undone_at_the_next_start(void) {
    settings_record_t r = SETTINGS_RECORD_DEFAULT;
    settings_t next = r.current;
    bool restart;
    next.channel = 11;
    strcpy(next.password, "typo in it");
    strcpy(next.ssid, "Typo");
    strcpy(next.country, "NL");
    next.led = 0;                                   // Saved in the same go: these stay
    next.auto_off_minutes = 30;
    next.tx_power = SETTINGS_TX_LOW;
    CHECK(settings_record_change(&r, &next, &restart) && restart);
    CHECK(settings_record_start(&r));
    CHECK(r.trial == 0 && r.current.channel == 6 && strcmp(r.current.password, SETTINGS_DEFAULT_PASSWORD) == 0);
    CHECK(r.current.ssid[0] == '\0' && strcmp(r.current.country, "XX") == 0);
    CHECK(r.current.led == 0 && r.current.auto_off_minutes == 30 && r.current.tx_power == SETTINGS_TX_LOW);
    CHECK(settings_record_sanitize(&r));
}

static void test_second_change_on_trial_keeps_the_confirmed_settings(void) {
    settings_record_t r = SETTINGS_RECORD_DEFAULT;
    settings_t next = r.current;
    bool restart;
    next.channel = 11;
    CHECK(settings_record_change(&r, &next, &restart) && restart);
    next.channel = 1;
    CHECK(settings_record_change(&r, &next, &restart) && restart);
    CHECK(r.current.channel == 1 && r.previous.channel == 6);    // Back to 6, not to 11
    CHECK(settings_record_start(&r) && r.current.channel == 6);
}

static void test_other_changes_need_no_restart_or_trial(void) {
    settings_record_t r = SETTINGS_RECORD_DEFAULT;
    settings_t next = r.current;
    bool restart;
    next.led = 0;
    next.tx_power = SETTINGS_TX_LOW;
    next.auto_off_minutes = 30;
    CHECK(settings_record_change(&r, &next, &restart) && !restart);
    CHECK(r.trial == 0 && r.current.led == 0);
    CHECK(!settings_record_change(&r, &next, &restart) && !restart);  // The same again: nothing to store

    // Each of the four Wi-Fi settings restarts the AP
    const char *what[] = {"ssid", "password", "country", "channel"};
    for (int i = 0; i < 4; i++) {
        settings_record_t t = SETTINGS_RECORD_DEFAULT;
        settings_t n = t.current;
        if (i == 0) strcpy(n.ssid, "Other");
        if (i == 1) strcpy(n.password, "different");
        if (i == 2) strcpy(n.country, "NL");
        if (i == 3) n.channel = 1;
        CHECK(settings_record_change(&t, &n, &restart) && restart);
        if (!restart) printf("  (%s)\n", what[i]);
    }
}

static void test_record_sanitize(void) {
    settings_record_t r = SETTINGS_RECORD_DEFAULT;
    r.trial = 7;
    r.previous.channel = 3;
    CHECK(!settings_record_sanitize(&r));
    CHECK(r.trial == 0 && r.previous.channel == 0);
    CHECK(settings_record_sanitize(&r));

    memset(&r, 0xFF, sizeof(r));                    // Erased flash
    CHECK(!settings_record_sanitize(&r));
    CHECK(memcmp(&r, &SETTINGS_RECORD_DEFAULT, sizeof(r)) == 0);
}

// --- POST /api/settings members ---------------------------------------------------------------------

static json_value_t str(const char *s) {
    json_value_t v = {.type = JSON_STRING};
    strcpy(v.string, s);
    return v;
}

static json_value_t num(long n, bool integer) {
    json_value_t v = {.type = JSON_NUMBER, .number = n, .integer = integer};
    return v;
}

static void test_set_member(void) {
    settings_t s = SETTINGS_DEFAULT;
    const char *msg = NULL;
    json_value_t v;

    v = str("Kitchen");
    CHECK(settings_set_member(&s, "ssid", &v, &msg) == SETTINGS_SET_OK && strcmp(s.ssid, "Kitchen") == 0);
    v = str("");
    CHECK(settings_set_member(&s, "ssid", &v, &msg) == SETTINGS_SET_OK && s.ssid[0] == '\0');
    v = str("0123456789012345678901234567890123");      // 34
    CHECK(settings_set_member(&s, "ssid", &v, &msg) == SETTINGS_SET_BAD_VALUE && msg);

    v = str("long enough");
    CHECK(settings_set_member(&s, "password", &v, &msg) == SETTINGS_SET_OK && strcmp(s.password, "long enough") == 0);
    v = str("short");
    CHECK(settings_set_member(&s, "password", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    v = str("fine\x80password");                     // A non-ASCII \u escape from the reader
    CHECK(settings_set_member(&s, "password", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    v = str("much longer than this");
    v.too_long = true;
    CHECK(settings_set_member(&s, "password", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    CHECK(strcmp(s.password, "long enough") == 0);  // Unchanged by the bad ones

    v = str("NL");
    CHECK(settings_set_member(&s, "country", &v, &msg) == SETTINGS_SET_OK && strcmp(s.country, "NL") == 0);
    v = str("QQ");
    CHECK(settings_set_member(&s, "country", &v, &msg) == SETTINGS_SET_BAD_VALUE);

    v = num(11, true);
    CHECK(settings_set_member(&s, "channel", &v, &msg) == SETTINGS_SET_OK && s.channel == 11);
    v = num(12, true);
    CHECK(settings_set_member(&s, "channel", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    v = num(-1, true);
    CHECK(settings_set_member(&s, "channel", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    v = num(0, false);                                  // 6.5
    CHECK(settings_set_member(&s, "channel", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    v = str("6");
    CHECK(settings_set_member(&s, "channel", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    CHECK(s.channel == 11);

    v = str("low");
    CHECK(settings_set_member(&s, "tx_power", &v, &msg) == SETTINGS_SET_OK && s.tx_power == SETTINGS_TX_LOW);
    v = str("max");
    CHECK(settings_set_member(&s, "tx_power", &v, &msg) == SETTINGS_SET_OK && s.tx_power == SETTINGS_TX_MAX);
    v = str("loud");
    CHECK(settings_set_member(&s, "tx_power", &v, &msg) == SETTINGS_SET_BAD_VALUE);
    v = num(1, true);
    CHECK(settings_set_member(&s, "tx_power", &v, &msg) == SETTINGS_SET_BAD_VALUE);

    v = num(0, true);
    CHECK(settings_set_member(&s, "auto_off_minutes", &v, &msg) == SETTINGS_SET_OK && s.auto_off_minutes == 0);
    v = num(7, true);
    CHECK(settings_set_member(&s, "auto_off_minutes", &v, &msg) == SETTINGS_SET_BAD_VALUE);

    v = (json_value_t){.type = JSON_BOOL, .boolean = false};
    CHECK(settings_set_member(&s, "led", &v, &msg) == SETTINGS_SET_OK && s.led == 0);
    v = num(1, true);
    CHECK(settings_set_member(&s, "led", &v, &msg) == SETTINGS_SET_BAD_VALUE);

    v = num(1, true);
    CHECK(settings_set_member(&s, "colour", &v, &msg) == SETTINGS_SET_UNKNOWN_FIELD);
    CHECK(settings_sanitize(&s));                       // Whatever was set is valid
}

int main(void) {
    test_defaults();
    test_ssid();
    test_password();
    test_country();
    test_numbers();
    test_sanitize_replaces_only_bad_values();
    test_settings_survive_a_reboot();
    test_trial_kept_once_a_client_comes();
    test_trial_undone_at_the_next_start();
    test_second_change_on_trial_keeps_the_confirmed_settings();
    test_other_changes_need_no_restart_or_trial();
    test_record_sanitize();
    test_set_member();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("settings: all tests passed\n");
    return 0;
}
