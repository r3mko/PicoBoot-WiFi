/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <string.h>
#include "payloadup.h"
#include "payload.h"

#define SECTOR                  4096u
#define PAGE                    256u
#define UF2_MAGIC0              0x0A324655u
#define UF2_MAGIC1              0x9E5D5157u
#define UF2_MAGIC_END           0x0AB16F30u
#define UF2_FLAG_NOT_MAIN_FLASH 0x00000001u
#define UF2_FLAG_FAMILY         0x00002000u
#define UF2_DATA_MAX            476u
#define HEAD                    12u         // "IPLBOOT " and the size: checked before any erase

_Static_assert(sizeof(((payloadup_t *)0)->sector) == SECTOR, "the writer buffers one flash sector");

const char *payloadup_result_code(payloadup_result_t r) {
    switch (r) {
        case PAYLOADUP_OK: return "ok";
        case PAYLOADUP_NOT_A_PAYLOAD: return "not_a_payload";
        case PAYLOADUP_BAD_UF2: return "bad_uf2";
        case PAYLOADUP_TOO_LARGE: return "too_large";
        case PAYLOADUP_BAD_PAYLOAD: return "bad_payload";
        case PAYLOADUP_BAD_READBACK: return "bad_readback";
        case PAYLOADUP_INCOMPLETE: return "incomplete";
        case PAYLOADUP_FLASH_WRITE: return "flash_write";
    }
    return "internal";
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

// Erases the slot's next sector and programs what the buffer holds; the first sector is only kept
static void flush_sector(payloadup_t *u) {
    if (!u->emptied) {
        if (!u->io->slot_emptied(u->io->ctx)) {
            u->result = PAYLOADUP_FLASH_WRITE;
            return;
        }
        u->emptied = true;
    }
    const uint32_t pages = (u->sector_fill + PAGE - 1) / PAGE;
    memset(u->sector + u->sector_fill, 0xFF, pages * PAGE - u->sector_fill);
    u->io->erase_sector(u->io->ctx, u->offset + u->written);
    if (u->written == 0) {
        memcpy(u->first, u->sector, pages * PAGE);  // Programmed last: it holds the header
        u->first_bytes = pages * PAGE;
    } else {
        u->io->program(u->io->ctx, u->offset + u->written, u->sector, pages * PAGE);
    }
    u->written += SECTOR;
    u->sector_fill = 0;
}

// The payload's bytes, in order: checked, hashed (the payload's own size bytes) and written
static void take(payloadup_t *u, const uint8_t *d, size_t n) {
    while (n && u->result == PAYLOADUP_OK) {
        if (u->payload_bytes >= u->size) {
            u->result = PAYLOADUP_TOO_LARGE;
            return;
        }
        size_t k = SECTOR - u->sector_fill;
        k = k < n ? k : n;
        if (u->payload_bytes < HEAD) {
            k = k < HEAD - u->payload_bytes ? k : HEAD - u->payload_bytes;
        }
        if (k > u->size - u->payload_bytes) {
            k = u->size - u->payload_bytes;
        }
        memcpy(u->sector + u->sector_fill, d, k);
        if (u->payload_bytes < HEAD) {
            memcpy(u->head + u->payload_bytes, d, k);
        }
        // The hash covers the payload's own bytes, not a UF2's padding after it
        if (u->payload_size) {
            const uint32_t end = u->payload_bytes + (uint32_t)k;
            if (u->payload_bytes < u->payload_size) {
                const uint32_t upto = end < u->payload_size ? end : u->payload_size;
                u->io->sha256_update(u->io->ctx, d, upto - u->payload_bytes);
            }
        }
        u->payload_bytes += (uint32_t)k;
        u->sector_fill += (uint32_t)k;
        d += k;
        n -= k;

        if (u->payload_bytes == HEAD && !u->payload_size) {
            // The header, before anything is erased
            if (memcmp(u->head, "IPLBOOT ", 8) != 0) {
                u->result = PAYLOADUP_NOT_A_PAYLOAD;
                return;
            }
            const uint32_t size = be32(u->head + 8);
            if (size < 36 || size % 4) {
                u->result = PAYLOADUP_BAD_PAYLOAD;
                return;
            }
            if (size > u->size) {
                u->result = PAYLOADUP_TOO_LARGE;
                return;
            }
            u->payload_size = size;
            u->io->sha256_start(u->io->ctx);
            u->io->sha256_update(u->io->ctx, u->head, HEAD);
        }
        if (u->sector_fill == SECTOR) {
            flush_sector(u);
        }
    }
}

// One 512-byte UF2 block: the payload's family, in order from 0x10080000 (D16)
static void uf2_block(payloadup_t *u) {
    const uint8_t *b = u->block;
    if (le32(b) != UF2_MAGIC0 || le32(b + 4) != UF2_MAGIC1 || le32(b + 508) != UF2_MAGIC_END) {
        u->result = PAYLOADUP_BAD_UF2;
        return;
    }
    const uint32_t flags = le32(b + 8), address = le32(b + 12), length = le32(b + 16);
    const uint32_t family = (flags & UF2_FLAG_FAMILY) ? le32(b + 28) : 0;
    if ((flags & UF2_FLAG_NOT_MAIN_FLASH) || address < PAYLOADUP_UF2_BASE) {
        return;                                 // Not the payload's
    }
    if (length > UF2_DATA_MAX) {
        u->result = PAYLOADUP_BAD_UF2;
        return;
    }
    if (!u->family_known) {
        if (address != PAYLOADUP_UF2_BASE) {
            return;                             // Not the payload's start: not its family (yet)
        }
        u->family = family;                     // Any family: one family's blocks (D16)
        u->family_known = true;
        u->next_address = PAYLOADUP_UF2_BASE;
    } else if (family != u->family) {
        return;                                 // The same payload for another chip (universal UF2s)
    }
    if (address != u->next_address) {
        u->result = PAYLOADUP_BAD_UF2;          // A gap, or out of order
        return;
    }
    u->next_address += length;
    take(u, b + 32, length);
}

void payloadup_begin(payloadup_t *u, const payloadup_io_t *io, uint32_t offset, uint32_t size, uint32_t upload_length) {
    memset(u, 0, sizeof(*u));
    u->io = io;
    u->offset = offset;
    u->size = size;
    u->upload_length = upload_length;
    u->result = upload_length < HEAD ? PAYLOADUP_NOT_A_PAYLOAD : PAYLOADUP_OK;
}

payloadup_result_t payloadup_feed(payloadup_t *u, const uint8_t *data, size_t length) {
    while (length && u->result == PAYLOADUP_OK) {
        if (u->received >= u->upload_length) {
            u->result = PAYLOADUP_BAD_PAYLOAD;  // More than Content-Length said
            break;
        }
        size_t n;
        if (u->format == FORMAT_UNKNOWN) {
            // The first 4 bytes tell a UF2 from a raw payload
            n = 4 - u->block_fill;
            n = n < length ? n : length;
            memcpy(u->block + u->block_fill, data, n);
            u->block_fill += (uint32_t)n;
            if (u->block_fill == 4) {
                if (le32(u->block) == UF2_MAGIC0) {
                    u->format = FORMAT_UF2;
                } else if (memcmp(u->block, "IPLB", 4) == 0) {
                    u->format = FORMAT_RAW;
                    u->block_fill = 0;
                    take(u, (const uint8_t *)"IPLB", 4);
                } else {
                    u->result = PAYLOADUP_NOT_A_PAYLOAD;
                }
            }
        } else if (u->format == FORMAT_RAW) {
            n = u->upload_length - u->received;     // Any more is caught at the next turn
            n = n < length ? n : length;
            take(u, data, n);
        } else {
            n = sizeof(u->block) - u->block_fill;
            n = n < length ? n : length;
            memcpy(u->block + u->block_fill, data, n);
            u->block_fill += (uint32_t)n;
            if (u->block_fill == sizeof(u->block)) {
                u->block_fill = 0;
                uf2_block(u);
            }
        }
        u->received += (uint32_t)n;
        data += n;
        length -= n;
    }
    return u->result;
}

payloadup_result_t payloadup_finish(payloadup_t *u) {
    if (u->result != PAYLOADUP_OK) {
        return u->result;
    }
    if (u->received != u->upload_length) {
        return u->result = PAYLOADUP_INCOMPLETE;
    }
    if (u->format == FORMAT_UF2 && u->block_fill) {
        return u->result = PAYLOADUP_BAD_UF2;   // Not whole blocks
    }
    if (!u->payload_size) {
        return u->result = PAYLOADUP_NOT_A_PAYLOAD;     // No payload blocks at 0x10080000, or too short
    }
    if (u->payload_bytes < u->payload_size) {
        return u->result = PAYLOADUP_BAD_PAYLOAD;       // Less data than its size says
    }
    if (u->sector_fill) {
        flush_sector(u);
        if (u->result != PAYLOADUP_OK) {
            return u->result;
        }
    }
    uint8_t sha[32];
    u->io->sha256_finish(u->io->ctx, sha);

    // The first sector last, then the payload as flash holds it
    u->io->program(u->io->ctx, u->offset, u->first, u->first_bytes);
    const uint8_t *flash = u->io->read(u->io->ctx, u->offset);
    if (payload_check((const uint32_t *)flash, u->size) == PAYLOAD_INVALID) {
        u->result = PAYLOADUP_BAD_PAYLOAD;      // The footer, most likely
    } else {
        uint8_t back[32];
        u->io->sha256_start(u->io->ctx);
        u->io->sha256_update(u->io->ctx, flash, u->payload_size);
        u->io->sha256_finish(u->io->ctx, back);
        if (memcmp(back, sha, 32) != 0) {
            u->result = PAYLOADUP_BAD_READBACK;
        }
    }
    if (u->result != PAYLOADUP_OK) {
        u->io->erase_sector(u->io->ctx, u->offset);     // No header: never a valid payload
        return u->result;
    }
    memcpy(u->sha256, sha, 32);
    return PAYLOADUP_OK;
}
