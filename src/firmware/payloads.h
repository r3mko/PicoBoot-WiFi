/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Payloads over Wi-Fi (design D15, D16, D17, D42): POST /api/payload streams a payload UF2 or a raw
// payload into the slot that isn't the choice, through payloadup.c, and makes it the choice once
// verified; the API's choose, rename and delete, and the payloads' status.

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "json.h"
#include "net.h"

// The upload hooks for net_start()
extern const net_upload_t PAYLOADS_UPLOAD;

// The "payloads" member of GET /api/status; usb_install: this boot switched to payload 1 because a
// USB install changed it (D17)
void payloads_status(json_out_t *j, bool usb_install);

// The slot names of the API: "payload1", "payload2", "builtin"; -1 for none of them
int payloads_slot(const char *name);
const char *payloads_name(uint8_t slot);

// POST /api/boot {"payload": ...}: the choice for the next power-ons (a payload must be valid).
// POST /api/payload/name and /delete. Each returns the HTTP status and writes an error into
// *code and *message, or nothing on success.
int payloads_choose(uint8_t slot, const char **code, const char **message);
int payloads_rename(uint8_t slot, const char *name, const char **code, const char **message);
int payloads_delete(uint8_t slot, const char **code, const char **message);

// A name the user may give (D42): printable ASCII, at most BOOTCTL_NAME_MAX characters
bool payloads_name_ok(const char *name);
