/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/settings.c (design D22, D40) and for settings as a record (D23).
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

static bool store(const settings_t *s) {
    return record_write(area, &flash, SETTINGS_MAGIC, SETTINGS_VERSION, s, sizeof(*s));
}

// What the firmware finds after a reboot
static const settings_t *load(void) {
    const record_header_t *rec = record_find(area, SETTINGS_MAGIC, SETTINGS_VERSION, sizeof(settings_t), NULL);
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

    settings_t s = SETTINGS_DEFAULT;
    strcpy(s.ssid, "GameCube");
    strcpy(s.password, "a new password");
    s.channel = 11;
    CHECK(store(&s));
    const settings_t *found = load();
    CHECK(found && memcmp(found, &s, sizeof(s)) == 0);

    // A power cut during the next change keeps the previous settings
    settings_t t = s;
    t.channel = 1;
    budget = 40;
    CHECK(!store(&t));
    budget = -1;
    found = load();
    CHECK(found && found->channel == 11 && strcmp(found->password, "a new password") == 0);

    // ...and the change goes through when tried again
    CHECK(store(&t));
    found = load();
    CHECK(found && found->channel == 1);
}

int main(void) {
    test_defaults();
    test_ssid();
    test_password();
    test_country();
    test_numbers();
    test_sanitize_replaces_only_bad_values();
    test_settings_survive_a_reboot();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("settings: all tests passed\n");
    return 0;
}
