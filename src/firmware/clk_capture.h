/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdint.h>
#include "clk_summary.h"

// Test builds only (design D47, plan step L2): a passive capture in pio1 samples CS and CLK every PIO
// cycle from the first rising CLK edge inside a CS low period, into a 64 KB RAM buffer through DMA:
// about 1 ms, BS1 and the first block's start. Call clk_capture_init() after timing_init(), clk_capture_start() right after
// timing_start(), and clk_capture_stop() after timing_stop().
void clk_capture_init(void);
void clk_capture_start(void);
void clk_capture_stop(void);

// The capture and its length in words (16 samples each), from clk_capture_stop() on: the download
// sends it as it is. It lasts until the Pico restarts; a boot without an injection has none.
const uint32_t *clk_capture_record(uint32_t *words);

// The summary of this boot's capture (clk_summary.h): nothing measured before clk_capture_stop(), and
// on a boot without an injection
void clk_capture_summary(clk_summary_t *s);
