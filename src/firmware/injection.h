/**
 * Copyright (c) 2025-2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INJECTION_PIN_CS    4   // U10 chip select
#define INJECTION_PIN_CLK   5   // EXI bus clock line
#define INJECTION_PIN_DATA  6   // Data pin used for output

typedef struct {
    const uint32_t *words;      // Read by the DMA; flash payloads through the untranslated XIP window
    size_t count;               // Words to send, a multiple of 256 (whole 1K transfers)
    bool byteswap;              // Flash payloads are stored big-endian; the built-in IPL is not
} injection_payload_t;

// Boosts the clock and sets up both state machines and the DMA, which starts filling the PIO FIFO.
// The state machines don't run yet. CS gets the D36 pull-up, or for tests PicoBoot's pull-down.
void injection_prepare(const injection_payload_t *payload, bool cs_pullup);

// Starts the state machines ("PIO ready"); the console's first CS may come any time after that.
void injection_go(void);

// True once the DMA has fed every word and the PIO has shifted out the last one and released
// the data line (clocked_output reached disable_chip).
bool injection_complete(void);

// Words the DMA hasn't fed yet
uint32_t injection_words_left(void);

// Ends the injection: lowers the clock and stops the state machines, as PicoBoot-DX does.
// If the injection didn't complete, it also stops the DMA and releases the data line.
void injection_finish(void);
