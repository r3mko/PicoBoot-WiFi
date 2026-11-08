/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Passive CS counter (design D31), in pio1. Call cs_counter_init() after the injection
// state machines are set up (they own the CS pin), and cs_counter_start() right before
// enabling them, so the counter sees every edge the injection sees.
void cs_counter_init(void);
void cs_counter_start(void);

// True once the console's first CS falling edge has been seen
bool cs_counter_first_edge_seen(void);

// Number of CS falling edges seen so far
uint32_t cs_counter_read(void);
