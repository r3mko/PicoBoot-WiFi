/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "bootctl.h"
#include "report.h"

// Boot control (design D23) and the boot log (D39) in flash. The writers erase and program flash
// with interrupts off: call them only when nothing reads flash in the background, so after the
// injection has ended (design C6) and from the main loop or an lwIP callback (poll mode).

// Copies the newest valid boot control into *out, or the defaults; returns true if there was one
bool storage_bootctl_load(bootctl_t *out);
bool storage_bootctl_store(const bootctl_t *b);

#define STORAGE_BOOTLOG_ENTRIES 32

bool storage_bootlog_append(const boot_report_t *r);
// Up to `max` reports, newest first, with their boot numbers; returns how many
unsigned storage_bootlog_read(boot_report_t *out, uint32_t *seq, unsigned max);
