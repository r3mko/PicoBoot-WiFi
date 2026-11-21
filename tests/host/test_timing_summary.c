/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/timing_summary.c (design D44's CS timing summary). Built and run by
// tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "timing_summary.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define NS_PER_COUNT 8u                 // 2 PIO cycles at 250 MHz
#define SHORT_GAP_NS 500u
#define MAX_WORDS 4096

static uint32_t rec[MAX_WORDS];
static uint32_t n;

// One CS low period as the recorder writes it: counts down from all ones
static void period(uint32_t high_counts, uint32_t clk, uint32_t low_counts) {
    rec[n++] = (clk << 31) | (0x7FFFFFFFu - high_counts);
    rec[n++] = 0xFFFFFFFFu - low_counts;
}

// A clean boot as the timing fit expects it, appended to the record: 224 BS1 transfers (1.3 us gaps,
// 3.0 us long), then blocks (50 us gaps, 257 us long), then one more short transfer
static void append_clean_boot(uint32_t blocks) {
    period(20000000, 0, 375);                       // 160 ms after the start
    for (uint32_t i = 1; i < 224; i++) {
        period(162 + i % 2, 0, 375);                // 1.296 or 1.304 us, 3.0 us
    }
    for (uint32_t i = 0; i < blocks; i++) {
        period(6250, 0, 32125 + i % 3);             // 50 us, 257.0 to 257.016 us
    }
    period(6300, 0, 375);
}

static void clean_boot(uint32_t blocks) {
    n = 0;
    append_clean_boot(blocks);
}

static void test_clean_boot(void) {
    timing_summary_t s;
    clean_boot(60);
    timing_summarize(rec, n, false, NS_PER_COUNT, SHORT_GAP_NS, &s);
    CHECK(s.transfers == 285);
    CHECK(s.first_gap_ns == 160000000);
    CHECK(s.bs1_gap_min_ns == 1296 && s.bs1_gap_max_ns == 1304);
    CHECK(s.bs1_low_min_ns == 3000 && s.bs1_low_max_ns == 3000);
    CHECK(s.payload_gap_min_ns == 50000 && s.payload_gap_max_ns == 50400);
    CHECK(s.low_225_ns == 257000 && s.low_226_ns == 257008);
    CHECK(s.short_gaps == 0 && s.first_short_gap == TIMING_NONE);
    CHECK(s.clk_high_falls == 0);
}

static void test_low_start_is_skipped(void) {
    // The dev console since 1.1.1: CS undriven and low at first, then driven high
    timing_summary_t s;
    n = 0;
    period(0, 0, 5000000);                          // The undriven start: 40 ms low
    append_clean_boot(60);
    timing_summarize(rec, n, true, NS_PER_COUNT, SHORT_GAP_NS, &s);
    CHECK(s.transfers == 285);
    CHECK(s.first_gap_ns == 160000000);
    CHECK(s.bs1_low_max_ns == 3000);
    // Without the flag, the start counts as a transfer, and everything shifts by one
    timing_summarize(rec, n, false, NS_PER_COUNT, SHORT_GAP_NS, &s);
    CHECK(s.transfers == 286 && s.first_gap_ns == 0 && s.bs1_low_max_ns == 40000000);
}

static void test_blip_shifts_the_split(void) {
    // A CS-high blip of 24 ns splits transfer 100: the injection would start one transfer early,
    // and the summary shows it as a BS1-sized transfer 225
    timing_summary_t s;
    clean_boot(60);
    memmove(rec + 2 * 101, rec + 2 * 100, (n - 2 * 100) * sizeof(rec[0]));
    n += 2;
    rec[2 * 100] = 0x7FFFFFFFu - 162;
    rec[2 * 100 + 1] = 0xFFFFFFFFu - 100;           // the first part, 0.8 us
    rec[2 * 101] = (1u << 31) | (0x7FFFFFFFu - 3);  // the blip, CLK high
    rec[2 * 101 + 1] = 0xFFFFFFFFu - 272;           // the second part
    timing_summarize(rec, n, false, NS_PER_COUNT, SHORT_GAP_NS, &s);
    CHECK(s.transfers == 286);
    CHECK(s.short_gaps == 1 && s.first_short_gap == 102);
    CHECK(s.bs1_gap_min_ns == 24);
    CHECK(s.bs1_low_min_ns == 800);
    CHECK(s.low_225_ns == 3000);                    // BS1's last real transfer, where a block should be
    CHECK(s.low_226_ns == 257000);
    CHECK(s.clk_high_falls == 1);
}

static void test_short_records(void) {
    timing_summary_t s;
    n = 0;
    timing_summarize(rec, n, false, NS_PER_COUNT, SHORT_GAP_NS, &s);
    CHECK(s.transfers == 0 && s.first_gap_ns == TIMING_NONE && s.bs1_gap_min_ns == TIMING_NONE);
    CHECK(s.low_225_ns == TIMING_NONE && s.payload_gap_max_ns == TIMING_NONE);
    period(100, 1, 200);
    rec[n++] = 0x7FFFFFFFu;                         // An incomplete last pair: CS fell, not risen yet
    timing_summarize(rec, n, false, NS_PER_COUNT, SHORT_GAP_NS, &s);
    CHECK(s.transfers == 1 && s.first_gap_ns == 800 && s.bs1_low_min_ns == 1600 && s.clk_high_falls == 1);
    CHECK(s.bs1_gap_min_ns == TIMING_NONE && s.short_gaps == 0);
    timing_summarize(rec, 2, true, NS_PER_COUNT, SHORT_GAP_NS, &s);  // only the start
    CHECK(s.transfers == 0);
}

static void test_saturation(void) {
    timing_summary_t s;
    n = 0;
    period(0x7FFFFFFFu, 0, 0xFFFFFFFFu);           // 17 s high and 34 s low: past the JSON's range
    timing_summarize(rec, n, false, NS_PER_COUNT, SHORT_GAP_NS, &s);
    CHECK(s.first_gap_ns == TIMING_NS_MAX && s.bs1_low_max_ns == TIMING_NS_MAX);
}

int main(void) {
    test_clean_boot();
    test_low_start_is_skipped();
    test_blip_shifts_the_split();
    test_short_records();
    test_saturation();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
