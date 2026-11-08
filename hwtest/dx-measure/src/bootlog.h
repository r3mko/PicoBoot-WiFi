/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"

// One boot's measurements, kept in a small log at the end of flash so several
// power-ons can be read back at once over Wi-Fi.

#define BOOT_FLAG_FLASH_PAYLOAD   (1u << 0)   // Valid flash payload (otherwise the built-in IPL)
#define BOOT_FLAG_FIRST_CS        (1u << 1)   // The console's first CS was seen
#define BOOT_FLAG_COMPLETE        (1u << 2)   // DMA done and clocked_output reached disable_chip
#define BOOT_FLAG_STALLED         (1u << 3)   // First CS seen, but the injection didn't complete in time
#define BOOT_FLAG_SKIPPED         (1u << 4)   // Not a power-on (design D8 rule): no injection
#define BOOT_FLAG_MARKER          (1u << 5)   // The POWMAN scratch session marker was already set

typedef struct {
    uint32_t magic;             // BOOTLOG_MAGIC
    uint32_t seq;               // Boot number, counts up across power-ons
    uint32_t chip_reset;        // POWMAN CHIP_RESET at the start of main()
    uint32_t watchdog_reason;   // WATCHDOG REASON at the start of main()
    uint32_t flags;             // BOOT_FLAG_*
    uint32_t payload_words;     // Words the DMA was set up to send (256 per 1K transfer)
    uint32_t words_delivered;   // Words the DMA actually sent
    uint32_t cs_at_end;         // CS falling edges counted when the injection ended
    uint32_t timer_at_main_us;  // Timer value at the start of main()
    uint32_t main_to_ready_us;  // main() to "PIO ready" (injection state machines enabled)
    int32_t  margin_us;         // "PIO ready" to the first CS, or -1 if not seen
    uint32_t injection_us;      // First CS to the end of the injection (complete or timed out)
    uint32_t reserved[51];
    uint32_t crc;               // CRC-32 of everything above
} boot_record_t;

_Static_assert(sizeof(boot_record_t) == 256, "one record per flash page");

#define BOOTLOG_MAGIC 0x50424D31u    // "PBM1"

// Appends a record (sets seq and crc). Erases flash, so call it only when nothing
// reads flash in the background: after the injection, before Wi-Fi starts.
void bootlog_append(boot_record_t *rec);

// Erases the whole log
void bootlog_clear(void);

// Copies up to max_count valid records into out, newest first; returns how many
uint bootlog_read(boot_record_t *out, uint max_count);

// Test switches, set from the report page and kept in their own flash sector, so tests
// don't need USB (and an open console). Reading them is two flash reads; setting them
// erases flash (same rules as bootlog_append).
#define SWITCH_FORCE_BUILTIN  (1u << 0)   // Boot the built-in IPL even with a valid flash payload
#define SWITCH_WIFI_LATE      (1u << 1)   // Next boot only: start Wi-Fi 60 s after the injection

uint32_t bootlog_switches(void);
void bootlog_set_switches(uint32_t switches);
