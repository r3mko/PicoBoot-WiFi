/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/clk_summary.c (design D47, plan step L2: the CLK capture's summary).
// Built and run by tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "clk_summary.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

#define NS 4u                           // One PIO cycle at 250 MHz
#define MAX_WORDS 40000

static uint32_t cap[MAX_WORDS];
static uint32_t samples;                // Samples written so far
static uint32_t last_cs, last_clk;      // The last sample's levels

// `n` samples of CS and CLK, packed as the capture does: 16 a word, the first in bits 31-30,
// CS in bit 0 and CLK in bit 1 of each
static void emit(uint32_t cs, uint32_t clk, uint32_t n) {
    for (uint32_t i = 0; i < n; i++, samples++) {
        const uint32_t word = samples / 16, shift = 2 * (15 - samples % 16);
        cap[word] = (cap[word] & ~(3u << shift)) | ((clk << 1 | cs) << shift);
    }
    if (n) {
        last_cs = cs;
        last_clk = clk;
    }
}

static void reset(void) {
    memset(cap, 0, sizeof(cap));
    samples = 0;
}

// A transfer's clocks with CS low: `clocks` rising edges, each high for `high` and low for `low`
// samples, after `setup` samples of CS low with CLK low; then CS high for `gap` samples
static void transfer(uint32_t clocks, uint32_t high, uint32_t low, uint32_t setup, uint32_t gap) {
    emit(0, 0, setup);
    for (uint32_t i = 0; i < clocks; i++) {
        emit(0, 1, high);
        emit(0, 0, low);
    }
    emit(1, 0, gap);
}

// The capture's start: inside the first transfer, just after its first rising edge
static void first_transfer(uint32_t clocks, uint32_t high, uint32_t low, uint32_t gap) {
    emit(0, 1, high - 1);
    emit(0, 0, low);
    transfer(clocks - 1, high, low, 0, gap);
}

// The capture is whole words: the last one is filled with the last levels, as the lines held them
static clk_summary_t summarize(void) {
    emit(last_cs, last_clk, (16 - samples % 16) % 16);
    clk_summary_t s;
    clk_summarize(cap, (samples + 15) / 16, NS, &s);
    return s;
}

static void test_no_capture(void) {
    clk_summary_t s;
    clk_summarize(cap, 0, NS, &s);
    CHECK(s.samples == 0 && s.transfers == 0 && s.bs1_edges == 0 && s.block_edges == 0);
    CHECK(s.bs1_high_min_ns == CLK_NONE && s.bs1_low_max_ns == CLK_NONE);
    CHECK(s.block_high_min_ns == CLK_NONE && s.block_low_max_ns == CLK_NONE);
}

// BS1 as T3 measured it, in samples: 96 clocks of about 9.3 samples, then the first block
static void test_a_clean_bs1_and_the_first_block(void) {
    reset();
    first_transfer(96, 5, 4, 36);
    for (uint32_t i = 1; i < 224; i++) {
        transfer(96, 5, 4, 10, 36);
    }
    transfer(400, 4, 5, 10, 0);             // The first block's start, still going as the capture ends
    clk_summary_t s = summarize();
    CHECK(s.samples == samples);
    CHECK(s.transfers == 225);
    CHECK(s.bs1_edges == 224 * 96 - 1);     // The first rising edge started the capture
    CHECK(s.bs1_high_min_ns == 5 * NS && s.bs1_high_max_ns == 5 * NS);
    CHECK(s.bs1_low_min_ns == 4 * NS && s.bs1_low_max_ns == 4 * NS);
    CHECK(s.block_edges == 400);
    CHECK(s.block_high_min_ns == 4 * NS && s.block_high_max_ns == 4 * NS);
    CHECK(s.block_low_min_ns == 5 * NS && s.block_low_max_ns == 5 * NS);
}

// Only whole phases count: not the first one, nor CLK low before the first edge or after the last
static void test_cut_runs_dont_count(void) {
    reset();
    emit(0, 1, 1);                          // The first high run, cut by the capture's start
    emit(0, 0, 7);
    emit(0, 1, 6);
    emit(0, 0, 3);
    emit(1, 0, 30);                         // CS rises in the middle of a low run
    emit(0, 0, 50);                         // Setup before the next transfer's first edge
    emit(0, 1, 4);
    emit(0, 0, 4);
    emit(1, 0, 20);
    clk_summary_t s = summarize();
    CHECK(s.transfers == 2);
    CHECK(s.bs1_edges == 2);
    CHECK(s.bs1_high_min_ns == 4 * NS && s.bs1_high_max_ns == 6 * NS);
    CHECK(s.bs1_low_min_ns == 7 * NS && s.bs1_low_max_ns == 7 * NS);
}

// The shortest and the longest phase over the whole of BS1
static void test_min_and_max(void) {
    reset();
    first_transfer(96, 5, 4, 36);
    transfer(10, 5, 4, 10, 36);
    emit(0, 0, 10);
    emit(0, 1, 3);                          // One short high phase...
    emit(0, 0, 6);                          // ...and one long low phase
    emit(0, 1, 5);
    emit(0, 0, 4);
    emit(1, 0, 36);
    clk_summary_t s = summarize();
    CHECK(s.bs1_high_min_ns == 3 * NS && s.bs1_high_max_ns == 5 * NS);
    CHECK(s.bs1_low_min_ns == 4 * NS && s.bs1_low_max_ns == 6 * NS);
}

// A CS blip inside a transfer cuts the runs around it and counts as a transfer of its own
static void test_a_cs_blip_inside_a_transfer(void) {
    reset();
    emit(0, 1, 4);
    emit(0, 0, 4);
    emit(0, 1, 5);                          // Whole: 5
    emit(1, 1, 2);                          // CS blips high while CLK is high
    emit(0, 1, 3);                          // Cut by the blip: doesn't count
    emit(0, 0, 4);                          // Whole: 4
    emit(0, 1, 5);
    emit(0, 0, 4);
    emit(1, 0, 20);
    clk_summary_t s = summarize();
    CHECK(s.transfers == 2);
    CHECK(s.bs1_high_min_ns == 5 * NS && s.bs1_high_max_ns == 5 * NS);
    CHECK(s.bs1_low_min_ns == 4 * NS && s.bs1_low_max_ns == 4 * NS);
}

// CLK edges while CS is high (another device on EXI channel 0) don't count
static void test_clk_with_cs_high(void) {
    reset();
    first_transfer(4, 5, 4, 10);
    emit(1, 1, 2);
    emit(1, 0, 2);
    emit(1, 1, 1);
    emit(1, 0, 10);
    clk_summary_t s = summarize();
    CHECK(s.transfers == 1);
    CHECK(s.bs1_edges == 3);
    CHECK(s.bs1_high_min_ns == 5 * NS && s.bs1_low_min_ns == 4 * NS);
}

int main(void) {
    test_no_capture();
    test_a_clean_bs1_and_the_first_block();
    test_cut_runs_dont_count();
    test_min_and_max();
    test_a_cs_blip_inside_a_transfer();
    test_clk_with_cs_high();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("clk_summary: all tests passed\n");
    return 0;
}
