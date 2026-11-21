/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "timing_summary.h"

// Test builds only (design D44): a passive recorder in pio1 times every CS-high and CS-low period
// of the IPL read, into a RAM buffer through DMA. Call timing_init() after counters_init(),
// timing_start() right after counters_start(), and timing_stop() after counters_stop().
void timing_init(void);
void timing_start(void);
void timing_stop(void);

// The record and its length in words (two per CS low period), from timing_stop() until the next
// power-on: the download sends it as it is.
const uint32_t *timing_record(uint32_t *words);

// The summary of this boot's record (timing_summary.h)
void timing_summary(bool cs_low_at_start, timing_summary_t *s);
