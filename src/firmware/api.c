/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "api.h"
#include "json.h"
#include "net.h"
#include "report.h"
#include "settings.h"
#include "storage.h"
#include "wifi.h"
#include "bootctl.h"
#include "bootinfo.h"
#include "bininfo.h"
#include "layout.h"
#include "slots.h"
#include "payloads.h"
#include "pico.h"                       // First: the board sets the chip, which platform_defs.h reads
#include "hardware/regs/addressmap.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/powman.h"
#include "pico/time.h"

static const page_info_t *info;
static boot_report_t log_rows[STORAGE_BOOTLOG_ENTRIES];
static uint32_t log_seq[STORAGE_BOOTLOG_ENTRIES];

void api_init(const page_info_t *i) {
    info = i;
}

static const char *slot_name(uint8_t slot) {
    return slot == BOOTCTL_SLOT_B ? "B" : "A";
}

static const char *state_name(uint8_t state) {
    switch (state) {
        case BOOTCTL_STATE_INSTALLED: return "installed";
        case BOOTCTL_STATE_EMPTY: return "empty";
        case BOOTCTL_STATE_TRIAL: return "trial";
        case BOOTCTL_STATE_KEPT: return "kept";
        case BOOTCTL_STATE_FAILED: return "failed";
        default: return "unknown";
    }
}

static const char *reset_name(uint32_t chip_reset) {
    if (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) return "POR";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS) return "BOR";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) return "RUN";
    if (chip_reset & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
                      POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS)) {
        return "WDOG";
    }
    return "other";
}

static const char *payload_name(payload_source_t p) {
    switch (p) {
        case PAYLOAD_SOURCE_PAYLOAD1: return "payload1";
        case PAYLOAD_SOURCE_PAYLOAD2: return "payload2";
        case PAYLOAD_SOURCE_BUILTIN: return "builtin";
        default: return "none";
    }
}

static const char *chosen_name(uint8_t chosen) {
    switch (chosen) {
        case BOOTCTL_PAYLOAD_1: return "payload1";
        case BOOTCTL_PAYLOAD_2: return "payload2";
        case BOOTCTL_PAYLOAD_BUILTIN: return "builtin";
        default: return "none";
    }
}

static const char *result_name(injection_result_t r) {
    switch (r) {
        case INJECTION_SKIPPED: return "skipped";
        case INJECTION_NO_CS: return "no_cs";
        case INJECTION_COMPLETE: return "complete";
        case INJECTION_STALLED: return "stalled";
        default: return "not_run";
    }
}

// --- Answers ------------------------------------------------------------------------------------

static int error(char *out, size_t size, size_t *len, int status, const char *code, const char *message,
                 const char *field) {
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_string(&j, "error", code);
    json_string(&j, "message", message);
    if (field) {
        json_string(&j, "field", field);
    }
    json_object_end(&j);
    *len = json_finish(&j);
    return status;
}

static int finish(json_out_t *j, char *out, size_t size, size_t *len) {
    *len = json_finish(j);
    if (*len == 0) {
        return error(out, size, len, 500, "internal", "the answer didn't fit", NULL);
    }
    return 200;
}

static int get_status(char *out, size_t size, size_t *len) {
    const boot_report_t *r = info->report;
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_int(&j, "api", 1);
    json_object_begin(&j, "firmware");
    json_string(&j, "version", PICOBOOT_WIFI_BUILD);
    json_string(&j, "slot", slot_name(r->slot));
    json_int(&j, "loader", r->loader_version);
    {
        // Both slots and the trial (D12, D13)
        bootctl_t b;
        storage_bootctl_load(&b);
        if (b.pending_slot == BOOTCTL_SLOT_A || b.pending_slot == BOOTCTL_SLOT_B) {
            json_string(&j, "pending", slot_name(b.pending_slot));
        } else {
            json_null(&j, "pending");
        }
        json_object_begin(&j, "states");
        json_string(&j, "A", state_name(b.slot_state[BOOTCTL_SLOT_A]));
        json_string(&j, "B", state_name(b.slot_state[BOOTCTL_SLOT_B]));
        json_object_end(&j);
        // The firmware in each slot (D13): this one's own build, the other's from its binary info
        json_object_begin(&j, "versions");
        for (uint8_t s = BOOTCTL_SLOT_A; s <= BOOTCTL_SLOT_B; s++) {
            char v[32];
            const uint8_t *image = (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE +
                                                     (s == BOOTCTL_SLOT_B ? LAYOUT_SLOT_B_OFFSET : LAYOUT_SLOT_A_OFFSET));
            if (s == r->slot) {
                json_string(&j, slot_name(s), PICOBOOT_WIFI_BUILD);
            } else if (b.slot_state[s] != BOOTCTL_STATE_EMPTY && bininfo_version(image, LAYOUT_SLOT_SIZE, v, sizeof(v))) {
                json_string(&j, slot_name(s), v);
            } else {
                json_null(&j, slot_name(s));
            }
        }
        json_object_end(&j);
    }
    json_object_end(&j);
    json_object_begin(&j, "chip");
    json_int(&j, "stepping", r->chip_version);
    json_int(&j, "rom", r->rom_version);
    json_object_end(&j);
    json_int(&j, "uptime_s", (long)(time_us_64() / 1000000));
    const float t = page_temperature_c();
    json_tenths(&j, "temperature_c", (long)(t * 10.0f + (t < 0 ? -0.5f : 0.5f)));
    json_object_begin(&j, "wifi");
    json_string(&j, "ssid", wifi_ssid());
    json_int(&j, "clients", net_client_count());
    json_bool(&j, "password_is_default", settings_default_password(wifi_settings()));
    json_bool(&j, "reverted", wifi_reverted());
    json_object_end(&j);
    payloads_status(&j, r->payload_usb_install);
    json_object_end(&j);
    return finish(&j, out, size, len);
}

static int get_settings(char *out, size_t size, size_t *len) {
    const settings_t *s = wifi_settings();
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_string(&j, "ssid", s->ssid);
    json_string(&j, "ssid_in_use", wifi_ssid());
    json_bool(&j, "password_is_default", settings_default_password(s));     // Never the password (D41)
    json_string(&j, "country", s->country);
    json_int(&j, "channel", s->channel);
    json_string(&j, "tx_power", SETTINGS_TX_NAMES[s->tx_power]);
    json_int(&j, "auto_off_minutes", s->auto_off_minutes);
    json_bool(&j, "led", s->led);
    json_object_begin(&j, "choices");
    json_array_begin(&j, "countries");
    for (const char *c = SETTINGS_COUNTRIES; *c; c += 2) {
        const char code[3] = {c[0], c[1], '\0'};
        json_string(&j, NULL, code);
    }
    json_array_end(&j);
    json_array_begin(&j, "channels");
    for (unsigned ch = 1; ch <= SETTINGS_CHANNEL_MAX; ch++) {
        json_int(&j, NULL, ch);
    }
    json_array_end(&j);
    json_array_begin(&j, "tx_power");
    for (unsigned i = 0; i < SETTINGS_TX_LEVELS; i++) {
        json_string(&j, NULL, SETTINGS_TX_NAMES[i]);
    }
    json_array_end(&j);
    json_array_begin(&j, "auto_off_minutes");
    for (unsigned i = 0; i < SETTINGS_AUTO_OFF_CHOICES; i++) {
        json_int(&j, NULL, SETTINGS_AUTO_OFF_MINUTES[i]);
    }
    json_array_end(&j);
    json_object_end(&j);
    json_object_end(&j);
    return finish(&j, out, size, len);
}

static void put_report(json_out_t *j, const char *key, const boot_report_t *r, uint32_t boot) {
    json_object_begin(j, key);
    json_int(j, "boot", (long)boot);
    json_bool(j, "power_on", r->power_on);
    json_string(j, "reset", reset_name(r->chip_reset));
    json_int(j, "chip_reset", (long)r->chip_reset);
    json_bool(j, "from_loader", r->from_loader);
    json_string(j, "slot", slot_name(r->slot));
    json_bool(j, "fallback", r->fallback);
    json_bool(j, "bootctl_default", r->bootctl_default);
    json_string(j, "payload", payload_name(r->payload));
    json_string(j, "payload_chosen", chosen_name(r->payload_chosen));
    json_bool(j, "payload_usb_install", r->payload_usb_install);
    json_int(j, "payload_words", (long)r->payload_words);
    json_int(j, "words_delivered", (long)r->words_delivered);
    json_string(j, "result", result_name(r->result));
    json_int(j, "main_to_ready_us", (long)r->main_to_ready_us);
    json_int(j, "margin_raw_us", (long)r->margin_raw_us);
    json_int(j, "margin_clocked_us", (long)r->margin_clocked_us);
    json_int(j, "injection_us", (long)r->injection_us);
    json_int(j, "cs_raw", (long)r->cs_raw);
    json_int(j, "cs_clocked", (long)r->cs_clocked);
    json_bool(j, "cs_high_at_ready", r->cs_high_at_ready);
    json_bool(j, "cs_pulldown_test", r->cs_pulldown_test);
    json_int(j, "chip_stepping", r->chip_version);
    json_int(j, "rom", r->rom_version);
    json_int(j, "loader", r->loader_version);
    json_bool(j, "trial", r->loader_flags & BOOTINFO_TRIAL);
    json_bool(j, "trial_failed", r->loader_flags & BOOTINFO_TRIAL_FAILED);
    json_bool(j, "crash_fallback", r->loader_flags & BOOTINFO_CRASH_FALLBACK);
    json_object_end(j);
}

static int get_diagnostics(char *out, size_t size, size_t *len) {
    const unsigned n = storage_bootlog_read(log_rows, log_seq, STORAGE_BOOTLOG_ENTRIES);
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    put_report(&j, "boot", info->report, info->boot_number);
    json_object_begin(&j, "wifi");
    json_string(&j, "blob", info->wifi_area == BOOTCTL_SLOT_B ? "B" : "A");
    json_int(&j, "up_ms", (long)info->wifi_up_ms);
    json_int(&j, "cyw43_init_ms", (long)info->cyw43_init_ms);
    json_int(&j, "ap_start_ms", (long)info->ap_start_ms);
    json_object_end(&j);
    json_array_begin(&j, "log");
    for (unsigned i = 0; i < n; i++) {
        put_report(&j, NULL, &log_rows[i], log_seq[i]);
    }
    json_array_end(&j);
    json_object_end(&j);
    return finish(&j, out, size, len);
}

// --- POST /api/settings ---------------------------------------------------------------------------

typedef struct {
    settings_t next;
    settings_set_t result;
    const char *message;
    char field[JSON_KEY_MAX + 1];
} update_t;

static bool on_member(void *ctx, const char *key, const json_value_t *v) {
    update_t *u = ctx;
    u->result = settings_set_member(&u->next, key, v, &u->message);
    if (u->result != SETTINGS_SET_OK) {
        strcpy(u->field, key);                  // Keys are at most JSON_KEY_MAX characters
        return false;
    }
    return true;
}

static int post_settings(const char *body, size_t body_len, char *out, size_t size, size_t *len) {
    update_t u = {.next = *wifi_settings()};
    switch (json_parse_object(body, body_len, on_member, &u)) {
        case JSON_SYNTAX:
            return error(out, size, len, 400, "bad_json", "the body must be a flat JSON object", NULL);
        case JSON_STOPPED:
            if (u.result == SETTINGS_SET_UNKNOWN_FIELD) {
                return error(out, size, len, 400, "unknown_field", "no such setting", u.field);
            }
            return error(out, size, len, 400, "bad_value", u.message, u.field);
        case JSON_OK:
            break;
    }
    bool restart;
    if (!wifi_change(&u.next, &restart)) {
        return error(out, size, len, 500, "flash_write", "writing flash failed; nothing changed", NULL);
    }
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_bool(&j, "saved", true);
    json_bool(&j, "wifi_restart", restart);
    json_string(&j, "ssid_in_use", wifi_ssid());    // The name to join after the restart
    json_object_end(&j);
    return finish(&j, out, size, len);
}

static bool no_member(void *ctx, const char *key, const json_value_t *v) {
    (void)v;
    strcpy(ctx, key);
    return false;
}

// --- POST /api/boot (D12, D13): {"keep": true} or {"slot": "A" | "B"} -------------------------------

typedef struct {
    int fields;
    bool keep;
    int slot;                           // BOOTCTL_SLOT_A / _B, or -1
    int payload;                        // BOOTCTL_PAYLOAD_*, or -1
    bool ipl_once;
    bool bad;
    char field[JSON_KEY_MAX + 1];
} boot_body_t;

static bool on_boot_member(void *ctx, const char *key, const json_value_t *v) {
    boot_body_t *bb = ctx;
    bb->fields++;
    strcpy(bb->field, key);
    if (strcmp(key, "keep") == 0) {
        bb->keep = v->type == JSON_BOOL && v->boolean;
        bb->bad = !bb->keep;
    } else if (strcmp(key, "slot") == 0) {
        bb->bad = v->type != JSON_STRING || (strcmp(v->string, "A") != 0 && strcmp(v->string, "B") != 0);
        bb->slot = bb->bad ? -1 : (v->string[0] == 'B' ? BOOTCTL_SLOT_B : BOOTCTL_SLOT_A);
    } else if (strcmp(key, "payload") == 0) {
        bb->payload = v->type == JSON_STRING ? payloads_slot(v->string) : -1;
        bb->bad = bb->payload < 0;
    } else if (strcmp(key, "original_ipl_once") == 0) {
        bb->ipl_once = v->type == JSON_BOOL;
        bb->bad = !bb->ipl_once;
        bb->ipl_once = bb->ipl_once && v->boolean;
    } else {
        return false;                   // Unknown field
    }
    return !bb->bad;
}

static int post_boot(const char *body, size_t body_len, char *out, size_t size, size_t *len) {
    boot_body_t bb = {.slot = -1, .payload = -1};
    switch (json_parse_object(body, body_len, on_boot_member, &bb)) {
        case JSON_SYNTAX:
            return error(out, size, len, 400, "bad_json", "the body must be a flat JSON object", NULL);
        case JSON_STOPPED:
            return bb.bad ? error(out, size, len, 400, "bad_value", "keep must be true; slot \"A\" or \"B\"; payload "
                                  "\"payload1\", \"payload2\" or \"builtin\"; original_ipl_once true or false", bb.field)
                          : error(out, size, len, 400, "unknown_field", "boot takes keep, slot, payload or original_ipl_once", bb.field);
        case JSON_OK:
            break;
    }
    if (bb.fields != 1) {
        return error(out, size, len, 400, "bad_json", "send exactly one of keep, slot, payload and original_ipl_once", NULL);
    }
    if (bb.payload >= 0) {
        // The payload for the next power-ons (D15)
        const char *code = NULL, *message = NULL;
        const int status = payloads_choose((uint8_t)bb.payload, &code, &message);
        if (status != 200) {
            return error(out, size, len, status, code, message, "payload");
        }
        json_out_t j;
        json_init(&j, out, size);
        json_object_begin(&j, NULL);
        json_string(&j, "payload", payloads_name((uint8_t)bb.payload));
        json_object_end(&j);
        return finish(&j, out, size, len);
    }
    if (strcmp(bb.field, "original_ipl_once") == 0) {
        // The next power-on boots the console's own IPL, once (D22)
        bootctl_t b;
        storage_bootctl_load(&b);
        b.flags = bb.ipl_once ? (uint8_t)(b.flags | BOOTCTL_FLAG_ORIGINAL_IPL_ONCE)
                              : (uint8_t)(b.flags & ~BOOTCTL_FLAG_ORIGINAL_IPL_ONCE);
        if (!storage_bootctl_store(&b)) {
            return error(out, size, len, 500, "flash_write", "writing flash failed; nothing changed", NULL);
        }
        json_out_t j;
        json_init(&j, out, size);
        json_object_begin(&j, NULL);
        json_bool(&j, "original_ipl_once", bb.ipl_once);
        json_object_end(&j);
        return finish(&j, out, size, len);
    }
    const uint8_t running = info->report->slot;
    bootctl_t b;
    storage_bootctl_load(&b);
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    if (bb.keep) {
        if (!slots_keep(&b, running)) {
            return error(out, size, len, 409, "not_on_trial", "this firmware isn't on trial: there's nothing to keep", NULL);
        }
        if (!storage_bootctl_store(&b)) {
            return error(out, size, len, 500, "flash_write", "writing flash failed; nothing changed", NULL);
        }
        json_bool(&j, "kept", true);
    } else {
        // Only into a slot that holds a PicoBoot-WiFi firmware: after a USB install the other slot
        // may hold anything (phase 3 review)
        char v[32];
        const uint8_t *image = (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE +
                                                 (bb.slot == BOOTCTL_SLOT_B ? LAYOUT_SLOT_B_OFFSET : LAYOUT_SLOT_A_OFFSET));
        if (bb.slot != running && !bininfo_version(image, LAYOUT_SLOT_SIZE, v, sizeof(v))) {
            return error(out, size, len, 409, "not_bootable", "that slot holds no PicoBoot-WiFi firmware", "slot");
        }
        const slots_switch_t r = slots_switch(&b, running, (uint8_t)bb.slot);
        if (r == SLOTS_NOT_BOOTABLE) {
            return error(out, size, len, 409, "not_bootable", "that slot is empty: upload a firmware into it first", "slot");
        }
        if (r == SLOTS_SWITCHED && !storage_bootctl_store(&b)) {
            return error(out, size, len, 500, "flash_write", "writing flash failed; nothing changed", NULL);
        }
        if (r == SLOTS_SWITCHED && b.pending_slot == bb.slot) {
            // On trial again: the loader tries it even if its last trial failed this session
            powman_hw->scratch[BOOTINFO_SCRATCH_SESSION] =
                slots_session_new_trial(powman_hw->scratch[BOOTINFO_SCRATCH_SESSION], (uint8_t)bb.slot);
        }
        json_bool(&j, "rebooting", r == SLOTS_SWITCHED);
        if (r == SLOTS_SWITCHED) {
            net_reboot_soon(false);     // Warm: a game keeps running (D8)
        }
    }
    json_object_end(&j);
    return finish(&j, out, size, len);
}

// --- POST /api/payload/name {"slot": ..., "name": ...} and /delete {"slot": ...} (D42) -----------------

typedef struct {
    int slot;                           // BOOTCTL_PAYLOAD_1 / _2, or -1
    bool has_name;
    char name[JSON_STRING_MAX + 1];
    bool bad;
    char field[JSON_KEY_MAX + 1];
} payload_body_t;

static bool on_payload_member(void *ctx, const char *key, const json_value_t *v) {
    payload_body_t *pb = ctx;
    strcpy(pb->field, key);
    if (strcmp(key, "slot") == 0) {
        pb->slot = v->type == JSON_STRING ? payloads_slot(v->string) : -1;
        pb->bad = pb->slot != BOOTCTL_PAYLOAD_1 && pb->slot != BOOTCTL_PAYLOAD_2;
    } else if (strcmp(key, "name") == 0) {
        pb->bad = v->type != JSON_STRING || v->too_long;
        pb->has_name = !pb->bad;
        strcpy(pb->name, v->string);
    } else {
        return false;
    }
    return !pb->bad;
}

static int post_payload(bool rename, const char *body, size_t body_len, char *out, size_t size, size_t *len) {
    payload_body_t pb = {.slot = -1};
    switch (json_parse_object(body, body_len, on_payload_member, &pb)) {
        case JSON_SYNTAX:
            return error(out, size, len, 400, "bad_json", "the body must be a flat JSON object", NULL);
        case JSON_STOPPED:
            return pb.bad ? error(out, size, len, 400, "bad_value", "slot must be \"payload1\" or \"payload2\"; name a string", pb.field)
                          : error(out, size, len, 400, "unknown_field", "takes slot (and name)", pb.field);
        case JSON_OK:
            break;
    }
    if (pb.slot < 0 || (rename && !pb.has_name)) {
        return error(out, size, len, 400, "bad_json", rename ? "send slot and name" : "send slot", NULL);
    }
    const char *code = NULL, *message = NULL;
    const int status = rename ? payloads_rename((uint8_t)pb.slot, pb.name, &code, &message)
                              : payloads_delete((uint8_t)pb.slot, &code, &message);
    if (status != 200) {
        return error(out, size, len, status, code, message, rename ? "name" : "slot");
    }
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_string(&j, "slot", payloads_name((uint8_t)pb.slot));
    if (rename) {
        json_string(&j, "name", pb.name);
    } else {
        json_bool(&j, "deleted", true);
    }
    json_object_end(&j);
    return finish(&j, out, size, len);
}

static int post_reboot(const char *body, size_t body_len, char *out, size_t size, size_t *len) {
    char field[JSON_KEY_MAX + 1];
    switch (json_parse_object(body, body_len, no_member, field)) {
        case JSON_SYNTAX:
            return error(out, size, len, 400, "bad_json", "the body must be {}", NULL);
        case JSON_STOPPED:
            return error(out, size, len, 400, "unknown_field", "reboot takes no fields", field);
        case JSON_OK:
            break;
    }
    net_reboot_soon(false);                     // Warm: no injection (design D8)
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_bool(&j, "rebooting", true);
    json_object_end(&j);
    return finish(&j, out, size, len);
}

// --- Routing --------------------------------------------------------------------------------------

int api_handle(bool post, const char *path, const char *body, size_t body_len,
               char *out, size_t out_size, size_t *out_len) {
    if (post && net_upload_running()) {
        // A reboot or an AP restart would cut the upload off (phase 3 review)
        return error(out, out_size, out_len, 503, "busy", "an upload is in progress: try again when it's done", NULL);
    }
    if (strcmp(path, "/api/status") == 0 || strcmp(path, "/api/diagnostics") == 0) {
        if (post) {
            return error(out, out_size, out_len, 405, "method_not_allowed", "use GET", NULL);
        }
        return path[5] == 's' ? get_status(out, out_size, out_len) : get_diagnostics(out, out_size, out_len);
    }
    if (strcmp(path, "/api/settings") == 0) {
        return post ? post_settings(body, body_len, out, out_size, out_len) : get_settings(out, out_size, out_len);
    }
    if (strcmp(path, "/api/boot") == 0) {
        if (!post) {
            return error(out, out_size, out_len, 405, "method_not_allowed", "use POST", NULL);
        }
        return post_boot(body, body_len, out, out_size, out_len);
    }
    if (strcmp(path, "/api/payload/name") == 0 || strcmp(path, "/api/payload/delete") == 0) {
        if (!post) {
            return error(out, out_size, out_len, 405, "method_not_allowed", "use POST", NULL);
        }
        return post_payload(path[13] == 'n', body, body_len, out, out_size, out_len);
    }
    if (strcmp(path, "/api/reboot") == 0) {
        if (!post) {
            return error(out, out_size, out_len, 405, "method_not_allowed", "use POST", NULL);
        }
        return post_reboot(body, body_len, out, out_size, out_len);
    }
    return error(out, out_size, out_len, 404, "not_found", "no such endpoint", NULL);
}
