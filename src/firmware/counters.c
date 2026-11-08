/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "counters.h"
#include "injection.h"
#include "counters.pio.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

// tests/test_counters.py mirrors this setup for its PIO simulator: keep both in sync

#define FLAG_RAW 2              // irq set 2 in cs_raw
#define FLAG_CLOCKED 3          // irq set 3 in cs_clocked
#define SNAPSHOT_TRIES 16

static const PIO counter_pio = pio1;
static uint raw_sm, clocked_sm;

static uint counter_init(const pio_program_t *program, pio_sm_config (*get_config)(uint)) {
    uint sm = pio_claim_unused_sm(counter_pio, true);
    uint offset = pio_add_program(counter_pio, program);

    pio_sm_config c = get_config(offset);
    sm_config_set_clkdiv(&c, 1.f);                       // Full system clock, like the injection
    sm_config_set_jmp_pin(&c, INJECTION_PIN_CS);          // cs_clocked's glitch check

    // No pio_gpio_init(): the counters only read CS and CLK, whose pads the injection set up
    pio_sm_init(counter_pio, sm, offset, &c);
    pio_sm_exec(counter_pio, sm, pio_encode_mov_not(pio_x, pio_null));   // X = 0xFFFFFFFF
    return sm;
}

void counters_init(void) {
    raw_sm = counter_init(&cs_raw_program, cs_raw_program_get_default_config);
    clocked_sm = counter_init(&cs_clocked_program, cs_clocked_program_get_default_config);
    pio_interrupt_clear(counter_pio, FLAG_RAW);
    pio_interrupt_clear(counter_pio, FLAG_CLOCKED);
}

void counters_start(void) {
    pio_set_sm_mask_enabled(counter_pio, (1u << raw_sm) | (1u << clocked_sm), true);
}

bool counters_first_raw_seen(void) {
    return pio_interrupt_get(counter_pio, FLAG_RAW);
}

bool counters_first_clocked_seen(void) {
    return pio_interrupt_get(counter_pio, FLAG_CLOCKED);
}

static uint32_t read_count(uint sm) {
    // The state machine is almost always stalled on a WAIT; exec'd instructions run in between
    pio_sm_exec(counter_pio, sm, pio_encode_mov(pio_isr, pio_x));
    pio_sm_exec(counter_pio, sm, pio_encode_push(false, false));
    return 0xFFFFFFFFu - pio_sm_get_blocking(counter_pio, sm);
}

void counters_snapshot(uint32_t *raw, uint32_t *clocked) {
    // A transfer whose CS has fallen but whose CLK hasn't risen yet would read as raw = clocked + 1,
    // so take both counts while CS is high and the raw count stays the same around them
    for (int i = 0; i < SNAPSHOT_TRIES; i++) {
        uint32_t before = read_count(raw_sm);
        bool cs_high = gpio_get(INJECTION_PIN_CS);
        *clocked = read_count(clocked_sm);
        *raw = read_count(raw_sm);
        if (cs_high && gpio_get(INJECTION_PIN_CS) && *raw == before) {
            return;
        }
    }
}
