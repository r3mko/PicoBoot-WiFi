/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "update.h"
#include "bootctl.h"
#include "layout.h"

#define SECTOR      LAYOUT_SECTOR_SIZE
#define PAGE        256u

// Header offsets (docs/update-format.md)
#define H_MAGIC         0
#define H_FORMAT        4
#define H_HEADER_SIZE   6
#define H_FLAGS         8
#define H_LAYOUT        12
#define H_LOADER        16
#define H_IMAGE_SIZE    20
#define H_BLOB_SIZE     24
#define H_RESERVED1     28
#define H_BOARD         32
#define H_VERSION       48
#define H_IMAGE_SHA     80
#define H_BLOB_SHA      112
#define H_RESERVED2     144
#define H_HEADER_SHA    224

#define WIFI_HEADER_SIZE 64u        // A Wi-Fi area image: the "W43B" header, then the data (mkwifiblob.py)

_Static_assert(sizeof(((update_t *)0)->sector) == SECTOR, "the writer buffers one flash sector");

const char *update_result_code(update_result_t r) {
    switch (r) {
        case UPDATE_OK: return "ok";
        case UPDATE_NOT_AN_UPDATE: return "not_an_update";
        case UPDATE_USB_INSTALL_FILE: return "not_an_update";
        case UPDATE_UNSUPPORTED_FORMAT: return "unsupported_format";
        case UPDATE_BAD_HEADER: return "bad_header";
        case UPDATE_WRONG_BOARD: return "wrong_board";
        case UPDATE_NEEDS_USB_INSTALL: return "needs_usb_install";
        case UPDATE_BAD_SIZE: return "bad_size";
        case UPDATE_NEEDS_BLOB: return "needs_blob";
        case UPDATE_BAD_IMAGE: return "bad_image";
        case UPDATE_BAD_BLOB: return "bad_blob";
        case UPDATE_INCOMPLETE: return "incomplete";
        case UPDATE_FLASH_WRITE: return "flash_write";
    }
    return "internal";
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static bool all_zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (p[i]) {
            return false;
        }
    }
    return true;
}

// A zero-padded field holding printable ASCII of at most size - 1 characters; copied into out
static bool text_field(const uint8_t *p, size_t size, char *out) {
    size_t n = 0;
    while (n < size && p[n]) {
        if (p[n] < 0x20 || p[n] > 0x7E) {
            return false;
        }
        n++;
    }
    if (n == size || !all_zero(p + n, size - n)) {
        return false;                       // No terminating zero, or something after it
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static void sha256(const update_io_t *io, const uint8_t *data, size_t length, uint8_t out[32]) {
    io->sha256_start(io->ctx);
    io->sha256_update(io->ctx, data, length);
    io->sha256_finish(io->ctx, out);
}

update_result_t update_check_header(const uint8_t *raw, const update_env_t *env, uint32_t upload_length,
                                    const update_io_t *io, update_header_t *out) {
    if (memcmp(raw + H_MAGIC, "PBWU", 4) != 0) {
        return memcmp(raw + H_MAGIC, "UF2\n", 4) == 0 ? UPDATE_USB_INSTALL_FILE : UPDATE_NOT_AN_UPDATE;
    }
    if (le16(raw + H_FORMAT) != UPDATE_FORMAT || le16(raw + H_HEADER_SIZE) != UPDATE_HEADER_SIZE) {
        return UPDATE_UNSUPPORTED_FORMAT;
    }
    uint8_t sha[32];
    sha256(io, raw, H_HEADER_SHA, sha);
    if (memcmp(sha, raw + H_HEADER_SHA, 32) != 0) {
        return UPDATE_BAD_HEADER;
    }
    update_header_t h;
    memset(&h, 0, sizeof(h));
    if (!text_field(raw + H_BOARD, sizeof(h.board), h.board) ||
        !text_field(raw + H_VERSION, sizeof(h.version), h.version)) {
        return UPDATE_BAD_HEADER;
    }
    if (strcmp(h.board, env->board) != 0) {
        return UPDATE_WRONG_BOARD;
    }
    h.flags = le32(raw + H_FLAGS);
    h.layout = le32(raw + H_LAYOUT);
    h.loader = le32(raw + H_LOADER);
    h.image_size = le32(raw + H_IMAGE_SIZE);
    h.blob_size = le32(raw + H_BLOB_SIZE);
    if (h.layout != env->layout || h.loader > env->loader) {
        return UPDATE_NEEDS_USB_INSTALL;
    }
    const bool blob = h.flags & UPDATE_FLAG_BLOB;
    if ((h.flags & ~UPDATE_FLAG_BLOB) || h.image_size == 0 || h.image_size > LAYOUT_SLOT_SIZE ||
        (blob ? h.blob_size < WIFI_HEADER_SIZE || h.blob_size > LAYOUT_WIFI_SIZE : h.blob_size != 0) ||
        !all_zero(raw + H_RESERVED1, 4) || !all_zero(raw + H_RESERVED2, H_HEADER_SHA - H_RESERVED2)) {
        return UPDATE_BAD_HEADER;
    }
    if (upload_length != UPDATE_HEADER_SIZE + h.image_size + h.blob_size) {
        return UPDATE_BAD_SIZE;
    }
    memcpy(h.image_sha256, raw + H_IMAGE_SHA, 32);
    memcpy(h.blob_sha256, raw + H_BLOB_SHA, 32);
    *out = h;
    return UPDATE_OK;
}

// --- The writer -----------------------------------------------------------------------------------

static uint32_t slot_offset(uint8_t slot) {
    return slot == BOOTCTL_SLOT_B ? LAYOUT_SLOT_B_OFFSET : LAYOUT_SLOT_A_OFFSET;
}

static uint32_t wifi_offset(uint8_t slot) {
    return slot == BOOTCTL_SLOT_B ? LAYOUT_WIFI_B_OFFSET : LAYOUT_WIFI_A_OFFSET;
}

// Whether the slot's paired Wi-Fi area holds exactly the image with this SHA-256 (D14)
static bool area_holds(const update_t *u, const uint8_t want[32]) {
    const uint8_t *area = u->io->read(u->io->ctx, wifi_offset(u->slot));
    if (memcmp(area, "W43B", 4) != 0) {
        return false;
    }
    const uint32_t data_len = le32(area + 16);
    if (data_len > LAYOUT_WIFI_SIZE - WIFI_HEADER_SIZE) {
        return false;
    }
    uint8_t sha[32];
    sha256(u->io, area, WIFI_HEADER_SIZE + data_len, sha);
    return memcmp(sha, want, 32) == 0;
}

static bool writing(const update_t *u) {
    return u->stage == UPDATE_STAGE_IMAGE || u->write_blob;
}

static void start_part(update_t *u, update_stage_t stage) {
    u->stage = stage;
    u->area_offset = stage == UPDATE_STAGE_IMAGE ? slot_offset(u->slot) : wifi_offset(u->slot);
    u->part_left = stage == UPDATE_STAGE_IMAGE ? u->header.image_size : u->header.blob_size;
    u->written = 0;
    u->sector_fill = 0;
    u->io->sha256_start(u->io->ctx);
}

// Erases the next sector of the current part and programs what the buffer holds
static void flush_sector(update_t *u) {
    if (!u->emptied) {
        // Before the first erase: boot control stops counting the slot as a firmware (D12)
        if (!u->io->slot_emptied(u->io->ctx, u->slot)) {
            u->result = UPDATE_FLASH_WRITE;
            return;
        }
        u->emptied = true;
    }
    const uint32_t pages = (u->sector_fill + PAGE - 1) / PAGE;
    memset(u->sector + u->sector_fill, 0xFF, pages * PAGE - u->sector_fill);
    u->io->erase_sector(u->io->ctx, u->area_offset + u->written);
    if (u->stage == UPDATE_STAGE_IMAGE && u->written == 0) {
        memcpy(u->first, u->sector, pages * PAGE);  // Programmed last, by end_part()
        u->first_bytes = pages * PAGE;
    } else {
        u->io->program(u->io->ctx, u->area_offset + u->written, u->sector, pages * PAGE);
    }
    u->written += SECTOR;
    u->sector_fill = 0;
}

static void end_part(update_t *u) {
    if (writing(u) && u->sector_fill) {
        flush_sector(u);
        if (u->result != UPDATE_OK) {
            return;
        }
    }
    uint8_t sha[32];
    u->io->sha256_finish(u->io->ctx, sha);
    const bool image = u->stage == UPDATE_STAGE_IMAGE;
    if (memcmp(sha, image ? u->header.image_sha256 : u->header.blob_sha256, 32) != 0) {
        u->result = image ? UPDATE_BAD_IMAGE : UPDATE_BAD_BLOB;
        return;                         // An image's first sector stays erased
    }
    if (image) {
        u->io->program(u->io->ctx, u->area_offset, u->first, u->first_bytes);
    }
    if (image && u->header.blob_size) {
        start_part(u, UPDATE_STAGE_BLOB);
    } else {
        u->stage = UPDATE_STAGE_DONE;
    }
}

static void end_header(update_t *u) {
    u->result = update_check_header(u->raw_header, &u->env, u->upload_length, u->io, &u->header);
    if (u->result != UPDATE_OK) {
        return;
    }
    // The blob goes into the paired Wi-Fi area only if that holds another one (D14)
    const bool has = area_holds(u, u->header.blob_sha256);
    if (!(u->header.flags & UPDATE_FLAG_BLOB) && !has) {
        u->result = UPDATE_NEEDS_BLOB;
        return;
    }
    u->write_blob = (u->header.flags & UPDATE_FLAG_BLOB) && !has;
    start_part(u, UPDATE_STAGE_IMAGE);
}

void update_begin(update_t *u, const update_io_t *io, const update_env_t *env, uint8_t slot, uint32_t upload_length) {
    memset(u, 0, sizeof(*u));
    u->io = io;
    u->env = *env;
    u->slot = slot;
    u->upload_length = upload_length;
    u->result = upload_length < UPDATE_HEADER_SIZE ? UPDATE_BAD_SIZE : UPDATE_OK;
    u->stage = UPDATE_STAGE_HEADER;
}

update_result_t update_feed(update_t *u, const uint8_t *data, size_t length) {
    while (length && u->result == UPDATE_OK) {
        size_t n;
        if (u->stage == UPDATE_STAGE_HEADER) {
            n = UPDATE_HEADER_SIZE - u->received;
            n = n < length ? n : length;
            memcpy(u->raw_header + u->received, data, n);
            u->received += (uint32_t)n;
            if (u->received == UPDATE_HEADER_SIZE) {
                end_header(u);
            }
        } else if (u->stage == UPDATE_STAGE_DONE) {
            u->result = UPDATE_BAD_SIZE;    // More than the header announced
            break;
        } else {
            n = u->part_left < length ? u->part_left : length;
            if (writing(u)) {
                n = n < SECTOR - u->sector_fill ? n : SECTOR - u->sector_fill;
                memcpy(u->sector + u->sector_fill, data, n);
                u->sector_fill += (uint32_t)n;
            }
            u->io->sha256_update(u->io->ctx, data, n);
            u->part_left -= (uint32_t)n;
            u->received += (uint32_t)n;
            if (writing(u) && u->sector_fill == SECTOR && u->part_left) {
                flush_sector(u);
            }
            if (u->part_left == 0 && u->result == UPDATE_OK) {
                end_part(u);
            }
        }
        data += n;
        length -= n;
    }
    return u->result;
}

update_result_t update_finish(update_t *u) {
    if (u->result != UPDATE_OK) {
        return u->result;
    }
    if (u->stage != UPDATE_STAGE_DONE) {
        return u->result = UPDATE_INCOMPLETE;
    }
    // Read back what was written: flash can fail where the hash as received was right
    uint8_t sha[32];
    sha256(u->io, u->io->read(u->io->ctx, slot_offset(u->slot)), u->header.image_size, sha);
    if (memcmp(sha, u->header.image_sha256, 32) != 0) {
        return u->result = UPDATE_BAD_IMAGE;
    }
    if (u->write_blob) {
        sha256(u->io, u->io->read(u->io->ctx, wifi_offset(u->slot)), u->header.blob_size, sha);
        if (memcmp(sha, u->header.blob_sha256, 32) != 0) {
            return u->result = UPDATE_BAD_BLOB;
        }
    }
    return UPDATE_OK;
}
