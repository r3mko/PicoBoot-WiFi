/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdbool.h>
#include "clk_summary.h"

static void take_min(uint32_t *min, uint32_t v) {
    if (*min == CLK_NONE || v < *min) {
        *min = v;
    }
}

static void take_max(uint32_t *max, uint32_t v) {
    if (*max == CLK_NONE || v > *max) {
        *max = v;
    }
}

// A whole phase of `level` lasting `samples`, which ended in `transfer`
static void phase(clk_summary_t *s, uint32_t transfer, uint32_t level, uint32_t samples, uint32_t ns_per_sample) {
    const uint32_t ns = samples * ns_per_sample;
    if (transfer <= CLK_SKIPPED) {
        take_min(level ? &s->bs1_high_min_ns : &s->bs1_low_min_ns, ns);
        take_max(level ? &s->bs1_high_max_ns : &s->bs1_low_max_ns, ns);
        uint16_t *count = &(level ? s->bs1_high_by_length : s->bs1_low_by_length)
                              [(samples < CLK_LENGTHS ? samples : CLK_LENGTHS) - 1];
        if (*count < UINT16_MAX) {
            (*count)++;
        }
    } else if (transfer == CLK_SKIPPED + 1) {
        take_min(level ? &s->block_high_min_ns : &s->block_low_min_ns, ns);
        take_max(level ? &s->block_high_max_ns : &s->block_low_max_ns, ns);
    }
}

void clk_summarize(const uint32_t *words, uint32_t count, uint32_t ns_per_sample, clk_summary_t *s) {
    *s = (clk_summary_t){
        0, 0, 0, CLK_NONE, CLK_NONE, CLK_NONE, CLK_NONE, 0, CLK_NONE, CLK_NONE, CLK_NONE, CLK_NONE, {0}, {0},
    };
    if (count == 0) {
        return;
    }
    s->samples = count * 16u;
    uint32_t cs_prev = (words[0] >> 30) & 1u, clk_prev = words[0] >> 31;    // The first sample
    s->transfers = cs_prev ? 0 : 1; // The capture starts at the first transfer's first clock, CS low
    uint32_t run = 0;               // Samples since the last CLK edge
    bool whole = false;             // That edge was inside this CS low period
    for (uint32_t i = 0; i < count; i++) {
        for (int k = 15; k >= 0; k--) {
            const uint32_t sample = (words[i] >> (2 * k)) & 3u;
            const uint32_t cs = sample & 1u, clk = sample >> 1;
            if (cs != cs_prev) {
                if (!cs) {
                    s->transfers++;
                }
                whole = false;      // A CS edge cuts the run short
                run = 0;
            } else if (!cs && clk != clk_prev) {
                if (whole) {
                    phase(s, s->transfers, clk_prev, run, ns_per_sample);
                }
                if (clk) {
                    if (s->transfers <= CLK_SKIPPED) {
                        s->bs1_edges++;
                    } else if (s->transfers == CLK_SKIPPED + 1) {
                        s->block_edges++;
                    }
                }
                whole = true;
                run = 0;
            }
            run++;
            cs_prev = cs;
            clk_prev = clk;
        }
    }
}
