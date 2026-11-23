/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdint.h>

// Design D47, plan step L2 (test builds only): the summary of the CLK capture. The capture
// (pio/clk_capture.pio) samples CS and CLK once a PIO cycle from the first rising CLK edge inside a
// CS low period: 16 samples a word, the first in bits 31-30; in each sample, bit 0 is CS and bit 1 is CLK.
// Plain C, so the host tests can run it (tests/host/test_clk_summary.c).
//
// A phase is whole when a CLK edge starts and ends it inside one CS low period. The run cut by
// the capture's start (just after the first rising edge) and runs cut by a CS edge don't count.
// Transfers are numbered from the capture's start: 1 if it starts with CS low, as the capture does.

#define CLK_SKIPPED     224u            // BS1's fetches: the first block is transfer 225 (D36)
#define CLK_NONE        0xFFFFFFFFu     // Nothing to measure
#define CLK_LENGTHS     8u              // BS1's phases counted by length: 1-7 samples, and 8 or more

typedef struct {
    uint32_t samples;               // Samples captured: 16 a word
    uint32_t transfers;             // CS low periods seen, the first counted though the capture starts inside it
    uint32_t bs1_edges;             // Rising CLK edges with CS low in transfers 1-224, after the first one
    uint32_t bs1_high_min_ns;       // CLK's whole high and low phases in transfers 1-224
    uint32_t bs1_high_max_ns;
    uint32_t bs1_low_min_ns;
    uint32_t bs1_low_max_ns;
    uint32_t block_edges;           // The same in transfer 225, the first block, as far as it was captured
    uint32_t block_high_min_ns;
    uint32_t block_high_max_ns;
    uint32_t block_low_min_ns;
    uint32_t block_low_max_ns;
    uint16_t bs1_high_by_length[CLK_LENGTHS];   // BS1's whole high phases of 1, 2, ... 7, and 8 or more
    uint16_t bs1_low_by_length[CLK_LENGTHS];    // samples (saturating), so one glitch shows as one
} clk_summary_t;

// Fills *s from `count` captured words; ns_per_sample: one PIO cycle. Values with nothing to
// measure are CLK_NONE; with no words, all of them are, and the counts are 0.
void clk_summarize(const uint32_t *words, uint32_t count, uint32_t ns_per_sample, clk_summary_t *s);
