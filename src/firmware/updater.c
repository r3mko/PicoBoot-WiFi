/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>
#include "updater.h"
#include "update.h"
#include "bootctl.h"
#include "json.h"
#include "layout.h"
#include "storage.h"
#include "slots.h"
#include "bootinfo.h"
#include "hardware/structs/powman.h"
#include "hardware/regs/addressmap.h"
#include "sha.h"

#ifndef PICOBOOT_WIFI_BOARD
#error "PICOBOOT_WIFI_BOARD (PICO_BOARD) comes from CMakeLists.txt"
#endif

extern char __flash_binary_start[], __flash_binary_end[];   // This image, from the SDK's linker script

static uint8_t running;
static update_env_t env = {PICOBOOT_WIFI_BOARD, LAYOUT_VERSION, 1};
static bool can_update;
static update_t writer;                 // One upload at a time (net.c makes sure)

void updater_init(uint8_t running_slot, uint32_t loader_version, bool from_loader) {
    running = running_slot;
    env.loader = loader_version;
    can_update = from_loader;
}

// --- update_io_t for this firmware ---------------------------------------------------------------

static void io_erase(void *ctx, uint32_t offset) {
    (void)ctx;
    storage_flash_erase(offset);
}

static void io_program(void *ctx, uint32_t offset, const uint8_t *data, size_t length) {
    (void)ctx;
    storage_flash_program(offset, data, length);
}

// Through the untranslated, uncached window: the loader maps only the running slot at XIP_BASE,
// and the cache mustn't hide what was just written
static const uint8_t *io_read(void *ctx, uint32_t offset) {
    (void)ctx;
    return (const uint8_t *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset);
}

// The RP2350's SHA-256 hardware (sha.c: an unfinished hash is released at the next start)
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

// Before the first erase: the slot stops being a firmware in boot control (D12)
static bool io_slot_emptied(void *ctx, uint8_t slot) {
    (void)ctx;
    bootctl_t b;
    storage_bootctl_load(&b);
    b.slot_state[slot] = BOOTCTL_STATE_EMPTY;
    if (b.pending_slot == slot) {
        b.pending_slot = BOOTCTL_NONE;
    }
    return storage_bootctl_store(&b);
}

static const update_io_t io = {io_erase, io_program, io_read, io_sha_start, io_sha_update, io_sha_finish,
                               io_slot_emptied, NULL};

// --- The backup (GET /api/backup/firmware, D42) --------------------------------------------------

bool updater_backup(int wifi_area, net_file_t *file) {
    static uint8_t header[UPDATE_HEADER_SIZE];      // Sent from here until the download ends
    const uint8_t *image = io_read(NULL, running == BOOTCTL_SLOT_B ? LAYOUT_SLOT_B_OFFSET : LAYOUT_SLOT_A_OFFSET);
    const uint8_t *blob = io_read(NULL, wifi_area == BOOTCTL_SLOT_B ? LAYOUT_WIFI_B_OFFSET : LAYOUT_WIFI_A_OFFSET);
    update_header_t h = {
        .flags = UPDATE_FLAG_BLOB,
        .layout = LAYOUT_VERSION,
        .loader = BOOTINFO_LOADER_NEEDED,
        .image_size = (uint32_t)(__flash_binary_end - __flash_binary_start),
        .blob_size = update_wifi_image_size(blob),
        .board = PICOBOOT_WIFI_BOARD,
        .version = PICOBOOT_WIFI_BUILD,
    };
    if ((wifi_area != BOOTCTL_SLOT_A && wifi_area != BOOTCTL_SLOT_B) || !h.blob_size) {
        return false;
    }
    sha_of(image, h.image_size, h.image_sha256);
    sha_of(blob, h.blob_size, h.blob_sha256);
    update_build_header(&h, header);
    sha_of(header, UPDATE_HEADER_HASHED, header + UPDATE_HEADER_HASHED);
    snprintf(file->filename, sizeof(file->filename), "picoboot-wifi-%s-update.bin", PICOBOOT_WIFI_BUILD);
    file->type = "application/octet-stream";
    file->parts[0].data = header;
    file->parts[0].length = UPDATE_HEADER_SIZE;
    file->parts[1].data = image;
    file->parts[1].length = h.image_size;
    file->parts[2].data = blob;
    file->parts[2].length = h.blob_size;
    return true;
}

// --- The upload hooks ----------------------------------------------------------------------------

static bool handles(const char *path) {
    return strcmp(path, "/api/firmware") == 0;
}

static const char *message(update_result_t r) {
    switch (r) {
        case UPDATE_NOT_AN_UPDATE: return "this isn't a PicoBoot-WiFi update file";
        case UPDATE_USB_INSTALL_FILE: return "this is the USB install file (a UF2): install it over USB instead";
        case UPDATE_UNSUPPORTED_FORMAT: return "this update file has a format this firmware doesn't know";
        case UPDATE_BAD_HEADER: return "the update file's header is damaged";
        case UPDATE_WRONG_BOARD: return "this update is for another board";
        case UPDATE_NEEDS_USB_INSTALL: return "this update needs a newer loader or another flash layout: install it over USB";
        case UPDATE_BAD_SIZE: return "the upload's length doesn't match the update file's header";
        case UPDATE_NEEDS_BLOB: return "this update file has no Wi-Fi blob, and the one in flash doesn't match";
        case UPDATE_BAD_IMAGE: return "the firmware image doesn't match its SHA-256 (damaged file or flash)";
        case UPDATE_BAD_BLOB: return "the Wi-Fi blob doesn't match its SHA-256 (damaged file or flash)";
        case UPDATE_INCOMPLETE: return "the upload stopped before the end";
        case UPDATE_FLASH_WRITE: return "writing boot control failed; the other slot isn't marked as a new firmware";
        case UPDATE_OK: break;
    }
    return "ok";
}

static int answer_error(update_result_t r, char *out, size_t size, size_t *len) {
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_string(&j, "error", update_result_code(r));
    json_string(&j, "message", message(r));
    json_object_end(&j);
    *len = json_finish(&j);
    return r == UPDATE_FLASH_WRITE ? 500 : 400;
}

static int begin(const char *path, const char *headers, uint16_t headers_len, uint32_t length,
                 char *out, size_t size, size_t *len) {
    (void)path;
    (void)headers;
    (void)headers_len;
    if (!can_update) {
        return answer_error(UPDATE_NEEDS_USB_INSTALL, out, size, len);
    }
    // Into the other slot, and only from the active firmware (D12; phase 3 review)
    bootctl_t b;
    storage_bootctl_load(&b);
    uint8_t target;
    const slots_update_t t = slots_update_target(&b, running, &target);
    if (t != SLOTS_UPDATE_OK) {
        json_out_t j;
        json_init(&j, out, size);
        json_object_begin(&j, NULL);
        json_string(&j, "error", t == SLOTS_UPDATE_ON_TRIAL ? "on_trial" : "not_active");
        json_string(&j, "message", t == SLOTS_UPDATE_ON_TRIAL ?
                    "this firmware is on trial: keep it first, or switch back to the other slot" :
                    "this firmware runs in place of the active one: boot the active slot, or wait for the reboot");
        json_object_end(&j);
        *len = json_finish(&j);
        return 409;
    }
    update_begin(&writer, &io, &env, target, length);
    return writer.result == UPDATE_OK ? 200 : answer_error(writer.result, out, size, len);
}

static void data(const uint8_t *bytes, size_t length) {
    update_feed(&writer, bytes, length);        // A failure sticks; the rest of the body is dropped
}

static int end(bool complete, char *out, size_t size, size_t *len) {
    update_result_t r = complete ? update_finish(&writer) : UPDATE_INCOMPLETE;
    sha_release();                              // Whatever the upload left
    if (r == UPDATE_OK) {
        // The slot holds the new firmware: on trial at the next start (D12)
        bootctl_t b;
        storage_bootctl_load(&b);
        b.slot_state[writer.slot] = BOOTCTL_STATE_TRIAL;
        b.pending_slot = writer.slot;
        if (!storage_bootctl_store(&b)) {
            r = UPDATE_FLASH_WRITE;
        } else {
            // A new firmware: the loader tries it even if the slot's last trial failed this session
            powman_hw->scratch[BOOTINFO_SCRATCH_SESSION] =
                slots_session_new_trial(powman_hw->scratch[BOOTINFO_SCRATCH_SESSION], writer.slot);
        }
    }
    if (r != UPDATE_OK) {
        return answer_error(r, out, size, len);
    }
    json_out_t j;
    json_init(&j, out, size);
    json_object_begin(&j, NULL);
    json_bool(&j, "installed", true);
    json_string(&j, "slot", writer.slot == BOOTCTL_SLOT_B ? "B" : "A");
    json_string(&j, "version", writer.header.version);
    json_bool(&j, "blob_written", writer.write_blob);
    json_bool(&j, "rebooting", true);
    json_object_end(&j);
    *len = json_finish(&j);
    net_reboot_soon(false);                     // Into the trial (D11); warm: a game keeps running (D8)
    return 200;
}

const net_upload_t UPDATER_UPLOAD = {handles, begin, data, end};
