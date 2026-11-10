/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>
#include "payloads.h"
#include "payloadup.h"
#include "payload.h"
#include "endian.h"
#include "bootctl.h"
#include "layout.h"
#include "sha.h"
#include "storage.h"
#include "pico.h"                       // First: the board sets the chip, which platform_defs.h reads
#include "hardware/regs/addressmap.h"

static const uint32_t slot_offset[2] = {LAYOUT_PAYLOAD1_OFFSET, LAYOUT_PAYLOAD2_OFFSET};
static const uint32_t slot_size[2] = {LAYOUT_PAYLOAD1_SIZE, LAYOUT_PAYLOAD2_SIZE};

// Read through the untranslated, uncached window, like the injection
static const uint32_t *slot_base(uint8_t slot) {
    return (const uint32_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + slot_offset[slot]);
}

// SHA-256 of each payload, computed once (it takes about 0.1 s for Swiss) and after each change
static struct {
    bool known;
    uint8_t sha256[32];
} cache[2];

int payloads_slot(const char *name) {
    if (strcmp(name, "payload1") == 0) return BOOTCTL_PAYLOAD_1;
    if (strcmp(name, "payload2") == 0) return BOOTCTL_PAYLOAD_2;
    if (strcmp(name, "builtin") == 0) return BOOTCTL_PAYLOAD_BUILTIN;
    return -1;
}

const char *payloads_name(uint8_t slot) {
    return slot == BOOTCTL_PAYLOAD_1 ? "payload1" : slot == BOOTCTL_PAYLOAD_2 ? "payload2" : "builtin";
}

bool payloads_name_ok(const char *name) {
    size_t n = 0;
    for (; name[n]; n++) {
        if (name[n] < 0x20 || name[n] > 0x7E) {
            return false;
        }
    }
    return n <= BOOTCTL_NAME_MAX;
}

static void hex(char *out, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        snprintf(out + 2 * i, 3, "%02x", b[i]);
    }
}

void payloads_status(json_out_t *j, bool usb_install) {
    bootctl_t b;
    storage_bootctl_load(&b);
    json_object_begin(j, "payloads");
    json_string(j, "chosen", payloads_name(b.active_payload < 3 ? b.active_payload : BOOTCTL_PAYLOAD_1));
    json_string(j, "upload_target", payloads_name(payload_upload_target(b.active_payload)));
    json_bool(j, "original_ipl_once", b.flags & BOOTCTL_FLAG_ORIGINAL_IPL_ONCE);
    json_bool(j, "usb_install", usb_install);
    for (uint8_t s = BOOTCTL_PAYLOAD_1; s <= BOOTCTL_PAYLOAD_2; s++) {
        json_object_begin(j, payloads_name(s));
        const size_t words = payload_check(slot_base(s), slot_size[s]);
        const uint32_t size = words ? BigEndian32(slot_base(s)[2]) : 0;
        json_bool(j, "valid", words != PAYLOAD_INVALID);
        json_string(j, "name", b.payload_name[s]);
        json_int(j, "size", (long)size);
        // Never while an upload holds the SHA hardware: it would wait for it here, inside lwIP
        if (words && !cache[s].known && !sha_busy()) {
            sha_of((const uint8_t *)slot_base(s), size, cache[s].sha256);
            cache[s].known = true;
        }
        if (words && cache[s].known) {
            char h[65];
            hex(h, cache[s].sha256, 32);
            json_string(j, "sha256", h);
        } else {
            json_null(j, "sha256");
        }
        json_object_end(j);
    }
    json_object_end(j);
}

// A choice, a rename or an upload over Wi-Fi settles a USB install of payload 1 that no console has taken yet
// (D17, main.c): its fingerprint is recorded, so the next power-on keeps what the user chose
static void settle_usb_install(bootctl_t *b) {
    b->payload1_fingerprint = payload_fingerprint(slot_base(BOOTCTL_PAYLOAD_1), slot_size[BOOTCTL_PAYLOAD_1]);
}

int payloads_choose(uint8_t slot, const char **code, const char **message) {
    if (slot != BOOTCTL_PAYLOAD_BUILTIN && payload_check(slot_base(slot), slot_size[slot]) == PAYLOAD_INVALID) {
        *code = "not_valid";
        *message = "that slot holds no valid payload";
        return 409;
    }
    bootctl_t b;
    storage_bootctl_load(&b);
    b.active_payload = slot;
    settle_usb_install(&b);
    if (!storage_bootctl_store(&b)) {
        *code = "flash_write";
        *message = "writing flash failed; nothing changed";
        return 500;
    }
    return 200;
}

int payloads_rename(uint8_t slot, const char *name, const char **code, const char **message) {
    if (!payloads_name_ok(name)) {
        *code = "bad_value";
        *message = "a name is at most 24 printable ASCII characters";
        return 400;
    }
    bootctl_t b;
    storage_bootctl_load(&b);
    memset(b.payload_name[slot], 0, sizeof(b.payload_name[slot]));
    memcpy(b.payload_name[slot], name, strlen(name));
    settle_usb_install(&b);
    if (!storage_bootctl_store(&b)) {
        *code = "flash_write";
        *message = "writing flash failed; nothing changed";
        return 500;
    }
    return 200;
}

bool payloads_file(uint8_t slot, net_file_t *file) {
    if (slot > BOOTCTL_PAYLOAD_2 || payload_check(slot_base(slot), slot_size[slot]) == PAYLOAD_INVALID) {
        return false;
    }
    bootctl_t b;
    storage_bootctl_load(&b);
    const char *name = b.payload_name[slot][0] ? b.payload_name[slot] : payloads_name(slot);
    size_t n = 0;
    for (; name[n] && n < BOOTCTL_NAME_MAX; n++) {
        // Safe in a quoted file name on any system: letters, digits and a few marks; others become _
        const char c = name[n];
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           strchr(" ._()+-", c) != NULL;
        file->filename[n] = plain ? c : '_';
    }
    strcpy(file->filename + n, ".bin");
    file->type = "application/octet-stream";
    file->parts[0].data = (const uint8_t *)slot_base(slot);
    file->parts[0].length = BigEndian32(slot_base(slot)[2]);
    return true;
}

int payloads_delete(uint8_t slot, const char **code, const char **message) {
    if (net_download_running()) {
        *code = "busy";                         // It may be this payload's backup
        *message = "a download is in progress: try again when it's done";
        return 503;
    }
    bootctl_t b;
    storage_bootctl_load(&b);
    if (b.active_payload == slot) {
        *code = "chosen";
        *message = "that payload is the choice: choose another one first";
        return 409;
    }
    // The name and (for payload 1) the fingerprint first, then the header: no valid payload any more
    b.payload_name[slot][0] = '\0';
    if (slot == BOOTCTL_PAYLOAD_1) {
        b.payload1_fingerprint = 0;             // No payload: not a USB install either (D17)
    }
    if (!storage_bootctl_store(&b)) {
        *code = "flash_write";
        *message = "writing flash failed; nothing changed";
        return 500;
    }
    storage_flash_erase(slot_offset[slot]);
    cache[slot].known = false;
    return 200;
}

// --- The upload (POST /api/payload) ---------------------------------------------------------------

static payloadup_t writer;              // One upload at a time (net.c makes sure)
static uint8_t target;
static char upload_name[BOOTCTL_NAME_MAX + 1];

static void io_erase(void *ctx, uint32_t offset) {
    (void)ctx;
    storage_flash_erase(offset);
}

static void io_program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    (void)ctx;
    storage_flash_program(offset, data, length);
}

static const uint8_t *io_read(void *ctx, uint32_t offset) {
    (void)ctx;
    return (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset);
}

static void io_sha_start(void *ctx) {
    (void)ctx;
    sha_start();
}

static void io_sha_update(void *ctx, const uint8_t *data, size_t length) {
    (void)ctx;
    sha_update(data, length);
}

static void io_sha_finish(void *ctx, uint8_t out[32]) {
    (void)ctx;
    sha_finish(out);
}

// Before the first erase: the slot loses its name, and payload 1 its fingerprint, so a failed
// upload into payload 1 isn't taken for a USB install at the next power-on (D17)
static bool io_slot_emptied(void *ctx) {
    (void)ctx;
    cache[target].known = false;
    bootctl_t b;
    storage_bootctl_load(&b);
    b.payload_name[target][0] = '\0';
    if (target == BOOTCTL_PAYLOAD_1) {
        b.payload1_fingerprint = 0;
    }
    return storage_bootctl_store(&b);
}

static const payloadup_io_t io = {io_erase, io_program, io_read, io_sha_start, io_sha_update, io_sha_finish,
                                  io_slot_emptied, NULL};

static bool handles(const char *path) {
    return strcmp(path, "/api/payload") == 0;
}

static int answer_error(int status, const char *code, const char *message, char *out, size_t size, size_t *len) {
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_string(&j, "error", code);
    json_string(&j, "message", message);
    json_object_end(&j);
    *len = json_finish(&j);
    return status;
}

static const char *message(payloadup_result_t r) {
    switch (r) {
        case PAYLOADUP_NOT_A_PAYLOAD: return "this isn't a payload: send a payload UF2 or a raw payload (.dol files are converted first)";
        case PAYLOADUP_BAD_UF2: return "the UF2 file is damaged, or its payload blocks aren't in order from 0x10080000";
        case PAYLOADUP_TOO_LARGE: return "the payload is larger than the slot";
        case PAYLOADUP_BAD_PAYLOAD: return "the payload's size or footer is wrong, or it's cut short";
        case PAYLOADUP_BAD_READBACK: return "the payload in flash doesn't match what came in";
        case PAYLOADUP_INCOMPLETE: return "the upload stopped before the end";
        case PAYLOADUP_FLASH_WRITE: return "writing boot control failed";
        case PAYLOADUP_OK: break;
    }
    return "ok";
}

static int begin(const char *path, const char *headers, uint16_t headers_len, uint32_t length,
                 char *out, size_t size, size_t *len) {
    (void)path;
    upload_name[0] = '\0';
    char name[64];
    if (net_header(headers, headers_len, "x-name:", name, sizeof(name)) && !payloads_name_ok(name)) {
        return answer_error(400, "bad_value", "X-Name is at most 24 printable ASCII characters", out, size, len);
    }
    if (net_header(headers, headers_len, "x-name:", name, sizeof(name))) {
        strcpy(upload_name, name);
    }
    bootctl_t b;
    storage_bootctl_load(&b);
    target = payload_upload_target(b.active_payload);
    payloadup_begin(&writer, &io, slot_offset[target], slot_size[target], length);
    if (writer.result != PAYLOADUP_OK) {
        return answer_error(400, payloadup_result_code(writer.result), message(writer.result), out, size, len);
    }
    return 200;
}

static void data(const uint8_t *bytes, size_t length) {
    payloadup_feed(&writer, bytes, length);     // A failure sticks; the rest of the body is dropped
}

static int end(bool complete, char *out, size_t size, size_t *len) {
    payloadup_result_t r = complete ? payloadup_finish(&writer) : PAYLOADUP_INCOMPLETE;
    sha_release();
    if (r == PAYLOADUP_OK) {
        // Verified: it becomes the choice (D15), with its name; payload 1's fingerprint is recorded, its
        // new one or one a USB install left unsettled (D17)
        bootctl_t b;
        storage_bootctl_load(&b);
        b.active_payload = target;
        memset(b.payload_name[target], 0, sizeof(b.payload_name[target]));
        memcpy(b.payload_name[target], upload_name, strlen(upload_name));
        settle_usb_install(&b);
        if (!storage_bootctl_store(&b)) {
            r = PAYLOADUP_FLASH_WRITE;
        }
        memcpy(cache[target].sha256, writer.sha256, 32);
        cache[target].known = true;
    }
    if (r != PAYLOADUP_OK) {
        return answer_error(r == PAYLOADUP_FLASH_WRITE ? 500 : 400, payloadup_result_code(r), message(r), out, size, len);
    }
    char h[65];
    hex(h, writer.sha256, 32);
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_bool(&j, "installed", true);
    json_string(&j, "slot", payloads_name(target));
    json_string(&j, "name", upload_name);
    json_int(&j, "size", (long)writer.payload_size);
    json_string(&j, "sha256", h);
    json_object_end(&j);
    *len = json_finish(&j);
    return 200;
}

const net_upload_t PAYLOADS_UPLOAD = {handles, begin, data, end};
