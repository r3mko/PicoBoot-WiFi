/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "clk_capture.h"
#include "injection.h"
#include "clk_capture.pio.h"
#include "pico/platform.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"

#define CAPTURE_WORDS   16384u      // 64 KB: 262,144 samples, 1.05 ms at 250 MHz, BS1 and the first block's start
#define NS_PER_SAMPLE   (1000000u / PICOBOOT_WIFI_INJECTION_KHZ)     // One PIO cycle: 4 ns at 250 MHz

_Static_assert(1000000u % PICOBOOT_WIFI_INJECTION_KHZ == 0, "a sample must be a whole number of nanoseconds");
_Static_assert(INJECTION_PIN_CLK == INJECTION_PIN_CS + 1, "in pins, 2 reads CS and CLK from IN_BASE = CS");
_Static_assert(CAPTURE_CS_PIN == INJECTION_PIN_CS && CAPTURE_CLK_PIN == INJECTION_PIN_CLK,
               "pio/clk_capture.pio waits on the injection's CS and CLK");

// Not zeroed at start-up, so the capture costs nothing before the injection, as D44's record
static uint32_t __uninitialized_ram(capture)[CAPTURE_WORDS];
static const PIO capture_pio = pio1;    // Next to the counters and D44's recorder: pio1's fourth state machine
static uint capture_sm;
static uint capture_dma;
static uint32_t captured;

// tests/test_clk_capture.py's add_capture() mirrors this set-up for its PIO simulator: keep both in sync
void clk_capture_init(void) {
    capture_sm = pio_claim_unused_sm(capture_pio, true);
    const uint offset = pio_add_program(capture_pio, &clk_capture_program);
    pio_sm_config c = clk_capture_program_get_default_config(offset);
    sm_config_set_clkdiv(&c, 1.f);                       // Full system clock: one sample a cycle
    sm_config_set_jmp_pin(&c, INJECTION_PIN_CS);          // The trigger's CS check
    sm_config_set_in_pins(&c, INJECTION_PIN_CS);          // CS in bit 0, CLK in bit 1
    sm_config_set_in_shift(&c, false, true, 32);          // Shift left, autopush at 32 bits: 16 samples
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);        // 8 words of slack for the DMA
    // No pio_gpio_init(): it only reads CS and CLK, whose pads the injection set up
    pio_sm_init(capture_pio, capture_sm, offset, &c);

    capture_dma = (uint)dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config(capture_dma);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_dreq(&d, pio_get_dreq(capture_pio, capture_sm, false));
    dma_channel_configure(capture_dma, &d, capture, &capture_pio->rxf[capture_sm], CAPTURE_WORDS, true);
}

void clk_capture_start(void) {
    pio_sm_set_enabled(capture_pio, capture_sm, true);
}

void clk_capture_stop(void) {
    pio_sm_set_enabled(capture_pio, capture_sm, false);
    // The DMA takes what the state machine pushed before it stopped, unless the buffer is full
    while (!pio_sm_is_rx_fifo_empty(capture_pio, capture_sm) && dma_channel_is_busy(capture_dma)) {
    }
    // Clear EN first (RP2350-E5), as timing_stop() does; the write address then counts every word
    hw_clear_bits(&dma_hw->ch[capture_dma].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(capture_dma);
    captured = (uint32_t)((const uint32_t *)dma_hw->ch[capture_dma].write_addr - capture);
}

const uint32_t *clk_capture_record(uint32_t *words) {
    *words = captured;
    return capture;
}

void clk_capture_summary(clk_summary_t *s) {
    clk_summarize(capture, captured, NS_PER_SAMPLE, s);
}
