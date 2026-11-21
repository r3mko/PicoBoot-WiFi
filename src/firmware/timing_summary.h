/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Design D44 (test builds only): the summary of the CS timing record. The recorder
// (pio/timing.pio) writes two words per CS low period, both counted down from all ones:
//   1. bit 31 = CLK as CS fell, bits 30-0 = the CS-high time before the fall;
//   2. the CS-low time.
// Plain C, so the host tests can run it (tests/host/test_timing_summary.c).

#define TIMING_SKIPPED  224u            // The skipped transfers before the payload (D36)
#define TIMING_NONE     0xFFFFFFFFu     // Nothing to measure: too few transfers
#define TIMING_NS_MAX   0x7FFFFFFFu     // Times saturate here (the API's JSON integers are signed)

typedef struct {
    uint32_t transfers;             // CS low periods recorded, after the undriven start if CS was low
    uint32_t first_gap_ns;          // CS high before the first one: from the start, or the console's first drive
    uint32_t bs1_gap_min_ns;        // CS high between transfers 1 and 224
    uint32_t bs1_gap_max_ns;
    uint32_t bs1_low_min_ns;        // CS low of transfers 1 to 224
    uint32_t bs1_low_max_ns;
    uint32_t payload_gap_min_ns;    // CS high before transfers 225 on
    uint32_t payload_gap_max_ns;
    uint32_t low_225_ns;            // CS low of transfers 225 and 226: a 1 KB block, or not
    uint32_t low_226_ns;
    uint32_t short_gaps;            // CS-high gaps after the first transfer shorter than short_gap_ns
    uint32_t first_short_gap;       // The transfer that followed the first one (1-based), or TIMING_NONE
    uint32_t clk_high_falls;        // CS falls with CLK high (D36's known limit)
} timing_summary_t;

// Fills *s from `words` recorded words (an incomplete last pair is ignored). cs_low_at_start: CS
// was low when the recorder started, so its first pair is that undriven start, not a transfer.
// ns_per_count: the time of one count (2 PIO cycles). Values without anything to measure are
// TIMING_NONE.
void timing_summarize(const uint32_t *record, uint32_t words, bool cs_low_at_start, uint32_t ns_per_count,
                      uint32_t short_gap_ns, timing_summary_t *s);
