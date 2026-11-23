/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Not a test of its own: prints clk_summarize()'s result for a capture given on stdin as 32-bit
// little-endian words, as GET /api/test/clk-capture downloads it. tests/test_clk_capture.py builds it
// to check the PIO capture and the C summary end to end (design D47, plan step L3).

#include <stdio.h>
#include "clk_summary.h"

#define MAX_WORDS 16384u        // clk_capture.c's buffer
#define NS_PER_SAMPLE 4u        // One PIO cycle at 250 MHz

static uint32_t words[MAX_WORDS];

static void print_lengths(const char *key, const uint16_t *n) {
    printf("%s", key);
    for (unsigned i = 0; i < CLK_LENGTHS; i++) {
        printf(" %u", (unsigned)n[i]);
    }
    printf("\n");
}

int main(void) {
    unsigned char b[4];
    uint32_t count = 0;
    while (count < MAX_WORDS && fread(b, 1, 4, stdin) == 4) {
        words[count++] = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
    }
    clk_summary_t s;
    clk_summarize(words, count, NS_PER_SAMPLE, &s);
    printf("samples %u\ntransfers %u\nbs1_edges %u\nblock_edges %u\n", (unsigned)s.samples, (unsigned)s.transfers,
           (unsigned)s.bs1_edges, (unsigned)s.block_edges);
    printf("bs1_high_min_ns %u\nbs1_high_max_ns %u\nbs1_low_min_ns %u\nbs1_low_max_ns %u\n", (unsigned)s.bs1_high_min_ns,
           (unsigned)s.bs1_high_max_ns, (unsigned)s.bs1_low_min_ns, (unsigned)s.bs1_low_max_ns);
    printf("block_high_min_ns %u\nblock_high_max_ns %u\nblock_low_min_ns %u\nblock_low_max_ns %u\n",
           (unsigned)s.block_high_min_ns, (unsigned)s.block_high_max_ns, (unsigned)s.block_low_min_ns,
           (unsigned)s.block_low_max_ns);
    print_lengths("bs1_high_by_length", s.bs1_high_by_length);
    print_lengths("bs1_low_by_length", s.bs1_low_by_length);
    return 0;
}
