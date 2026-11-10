/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The JSON API (docs/api.md): status, settings, diagnostics, boot choices, payloads, reboot, and
// the backups (GET /api/backup/..., D42).

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "page.h"
#include "net.h"

// Keeps the boot's information (the same as the page's)
void api_init(const page_info_t *info);

// The downloads for net_start(): GET /api/backup/payload1, /payload2, /firmware and /settings
extern const net_download_t API_DOWNLOAD;

// net_api_fn: handles a GET or a POST under /api/
int api_handle(bool post, const char *path, const char *body, size_t body_len,
               char *out, size_t out_size, size_t *out_len);
