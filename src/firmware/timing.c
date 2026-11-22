/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "timing.h"
#include "injection.h"
#include "timing.pio.h"
#include "pico/platform.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"

#define RECORD_WORDS    8192u       // 4096 CS low periods: BS1, the largest payload and many extras (32 KB)
#define NS_PER_COUNT    (2000000u / PICOBOOT_WIFI_INJECTION_KHZ)     // 2 PIO cycles: 8 ns at 250 MHz
#define SHORT_GAP_NS    100u        // A CS-high gap this short is a blip: real ones are 136 ns or more (T3)

_Static_assert(2000000u % PICOBOOT_WIFI_INJECTION_KHZ == 0, "a count must be a whole number of nanoseconds");

// Not zeroed at start-up, so the record costs nothing before the injection (phase 4's lesson)
static uint32_t __uninitialized_ram(record)[RECORD_WORDS];
static const PIO timing_pio = pio1;     // Next to the counters (counters.c)
static uint timing_sm;
static uint timing_dma;
static uint32_t recorded;

// tests/test_timing.py's add_recorder() mirrors this set-up for its PIO simulator: keep both in sync
void timing_init(void) {
    timing_sm = pio_claim_unused_sm(timing_pio, true);
    const uint offset = pio_add_program(timing_pio, &cs_timing_program);
    pio_sm_config c = cs_timing_program_get_default_config(offset);
    sm_config_set_clkdiv(&c, 1.f);                       // Full system clock, like the counters
    sm_config_set_jmp_pin(&c, INJECTION_PIN_CS);
    sm_config_set_in_pins(&c, INJECTION_PIN_CLK);
    sm_config_set_in_shift(&c, false, true, 32);          // Shift left, autopush at 32 bits
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);        // 8 words of slack for the DMA
    // No pio_gpio_init(): it only reads CS and CLK, whose pads the injection set up
    pio_sm_init(timing_pio, timing_sm, offset, &c);

    timing_dma = (uint)dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config(timing_dma);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, false);
    channel_config_set_write_increment(&d, true);
    channel_config_set_dreq(&d, pio_get_dreq(timing_pio, timing_sm, false));
    dma_channel_configure(timing_dma, &d, record, &timing_pio->rxf[timing_sm], RECORD_WORDS, true);
}

void timing_start(void) {
    pio_sm_set_enabled(timing_pio, timing_sm, true);
}

void timing_stop(void) {
    pio_sm_set_enabled(timing_pio, timing_sm, false);
    // The DMA takes what the state machine pushed before it stopped, unless the record is full
    while (!pio_sm_is_rx_fifo_empty(timing_pio, timing_sm) && dma_channel_is_busy(timing_dma)) {
    }
    // Clear EN first (RP2350-E5), as injection_finish() does. The abort waits for a write in flight,
    // so the write address then counts every word the record holds.
    hw_clear_bits(&dma_hw->ch[timing_dma].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(timing_dma);
    recorded = (uint32_t)((const uint32_t *)dma_hw->ch[timing_dma].write_addr - record);
}

const uint32_t *timing_record(uint32_t *words) {
    *words = recorded;
    return record;
}

void timing_summary(timing_summary_t *s) {
    timing_summarize(record, recorded, NS_PER_COUNT, SHORT_GAP_NS, s);
}
