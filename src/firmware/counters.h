/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Passive CS counters for the boot diagnostics (design D31, D36), in pio1.
// Call counters_init() after injection_prepare() (which sets up the CS and CLK pads), and
// counters_start() right before injection_go(), so they see every edge the injection sees.
void counters_init(void);
void counters_start(void);

// True once the first CS falling edge (raw) / the first real, clocked transfer has been seen
bool counters_first_raw_seen(void);
bool counters_first_clocked_seen(void);

// Both counts, read consistently: while CS is high and the raw count isn't changing. A raw
// count above the clocked count means CS dropped without a transfer: a glitch (design D36).
void counters_snapshot(uint32_t *raw, uint32_t *clocked);
