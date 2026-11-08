/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "measure.h"
#include "hardware/pio.h"
#include "measure.pio.h"

// tests/test_measure.py mirrors this setup for its PIO simulator: keep both in sync

static const PIO counter_pio = pio1;
static uint counter_sm;

void cs_counter_init(void) {
    counter_sm = pio_claim_unused_sm(counter_pio, true);
    uint offset = pio_add_program(counter_pio, &cs_counter_program);

    pio_sm_config c = cs_counter_program_get_default_config(offset);
    // Run at full system clock, like the injection state machines
    sm_config_set_clkdiv(&c, 1.f);

    // No pio_gpio_init(): the counter only reads CS, whose pad picoboot-dx's PIO already set up
    pio_sm_init(counter_pio, counter_sm, offset, &c);

    // X = 0xFFFFFFFF; the program counts it down once per CS falling edge
    pio_sm_exec(counter_pio, counter_sm, pio_encode_mov_not(pio_x, pio_null));
    pio_interrupt_clear(counter_pio, 0);
}

void cs_counter_start(void) {
    pio_sm_set_enabled(counter_pio, counter_sm, true);
}

bool cs_counter_first_edge_seen(void) {
    return pio_interrupt_get(counter_pio, 0);
}

uint32_t cs_counter_read(void) {
    // The state machine is almost always stalled on a WAIT; exec'd instructions run in between
    pio_sm_exec(counter_pio, counter_sm, pio_encode_mov(pio_isr, pio_x));
    pio_sm_exec(counter_pio, counter_sm, pio_encode_push(false, false));
    return 0xFFFFFFFFu - pio_sm_get_blocking(counter_pio, counter_sm);
}
