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

// Boosts the clock and sets up the chosen method's state machines and the DMA, which starts filling
// the PIO FIFO (D48). Standard: D36's count of 224, on_transfer and clocked_output in pio0. By length
// (D47): the detector in pio1 and clocked_output_length in pio0. The state machines don't run yet.
// CS keeps the pad's reset pull-down, as PicoBoot leaves it (D36).
void injection_prepare(const injection_payload_t *payload, bool by_length);

// Starts the state machines ("PIO ready"); the console's first CS may come any time after that.
void injection_go(void);

// True once the DMA has fed every word and the PIO has shifted out the last one and released
// the data line (clocked_output reached disable_chip).
bool injection_complete(void);

// Words the DMA hasn't fed yet
uint32_t injection_words_left(void);

// Ends the injection: lowers the clock and stops the state machines, as PicoBoot-DX does, puts the
// bus priority back and turns the data pin's input off. If the injection didn't complete, it first
// stops the DMA and releases the data line.
void injection_finish(void);
