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
static uint counter_sm, qualified_sm;

static uint counter_sm_init(const pio_program_t *program, pio_sm_config (*get_config)(uint)) {
    uint sm = pio_claim_unused_sm(counter_pio, true);
    uint offset = pio_add_program(counter_pio, program);

    pio_sm_config c = get_config(offset);
    // Run at full system clock, like the injection state machines
    sm_config_set_clkdiv(&c, 1.f);

    // No pio_gpio_init(): the counters only read CS and CLK, whose pads picoboot-dx's PIO set up
    pio_sm_init(counter_pio, sm, offset, &c);

    // X = 0xFFFFFFFF; the program counts it down
    pio_sm_exec(counter_pio, sm, pio_encode_mov_not(pio_x, pio_null));
    return sm;
}

void cs_counter_init(void) {
    counter_sm = counter_sm_init(&cs_counter_program, cs_counter_program_get_default_config);
    qualified_sm = counter_sm_init(&cs_qualified_program, cs_qualified_program_get_default_config);
    pio_interrupt_clear(counter_pio, 0);
    pio_interrupt_clear(counter_pio, 1);
}

void cs_counter_start(void) {
    pio_set_sm_mask_enabled(counter_pio, (1u << counter_sm) | (1u << qualified_sm), true);
}

bool cs_counter_first_edge_seen(void) {
    return pio_interrupt_get(counter_pio, 0);
}

static uint32_t read_count(uint sm) {
    // The state machine is almost always stalled on a WAIT; exec'd instructions run in between
    pio_sm_exec(counter_pio, sm, pio_encode_mov(pio_isr, pio_x));
    pio_sm_exec(counter_pio, sm, pio_encode_push(false, false));
    return 0xFFFFFFFFu - pio_sm_get_blocking(counter_pio, sm);
}

uint32_t cs_counter_read(void) {
    return read_count(counter_sm);
}

bool cs_qualified_first_seen(void) {
    return pio_interrupt_get(counter_pio, 1);
}

uint32_t cs_qualified_read(void) {
    return read_count(qualified_sm);
}
