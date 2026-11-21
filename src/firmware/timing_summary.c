/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "timing_summary.h"

static uint32_t ns(uint32_t counts, uint32_t ns_per_count) {
    const uint64_t v = (uint64_t)counts * ns_per_count;
    return v > TIMING_NS_MAX ? TIMING_NS_MAX : (uint32_t)v;
}

static void take_min(uint32_t *min, uint32_t v) {
    if (*min == TIMING_NONE || v < *min) {
        *min = v;
    }
}

static void take_max(uint32_t *max, uint32_t v) {
    if (*max == TIMING_NONE || v > *max) {
        *max = v;
    }
}

void timing_summarize(const uint32_t *record, uint32_t words, bool cs_low_at_start, uint32_t ns_per_count,
                      uint32_t short_gap_ns, timing_summary_t *s) {
    *s = (timing_summary_t){
        0, TIMING_NONE, TIMING_NONE, TIMING_NONE, TIMING_NONE, TIMING_NONE, TIMING_NONE, TIMING_NONE,
        TIMING_NONE, TIMING_NONE, 0, TIMING_NONE, 0,
    };
    uint32_t pairs = words / 2;
    if (cs_low_at_start && pairs > 0) {
        record += 2;
        pairs--;
    }
    s->transfers = pairs;
    for (uint32_t i = 0; i < pairs; i++) {
        const uint32_t gap = ns(0x7FFFFFFFu - (record[2 * i] & 0x7FFFFFFFu), ns_per_count);
        const uint32_t low = ns(0xFFFFFFFFu - record[2 * i + 1], ns_per_count);
        if (record[2 * i] >> 31) {
            s->clk_high_falls++;
        }
        if (i == 0) {
            s->first_gap_ns = gap;
        } else {
            if (i < TIMING_SKIPPED) {
                take_min(&s->bs1_gap_min_ns, gap);
                take_max(&s->bs1_gap_max_ns, gap);
            } else {
                take_min(&s->payload_gap_min_ns, gap);
                take_max(&s->payload_gap_max_ns, gap);
            }
            if (gap < short_gap_ns) {
                if (s->short_gaps++ == 0) {
                    s->first_short_gap = i + 1;
                }
            }
        }
        if (i < TIMING_SKIPPED) {
            take_min(&s->bs1_low_min_ns, low);
            take_max(&s->bs1_low_max_ns, low);
        } else if (i == TIMING_SKIPPED) {
            s->low_225_ns = low;
        } else if (i == TIMING_SKIPPED + 1) {
            s->low_226_ns = low;
        }
    }
}
