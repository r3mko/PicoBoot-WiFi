/**
 * Copyright (c) 2025 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "injection_pio.h"
#include "injection.pio.h"
#include "length.pio.h"

// From PicoBoot-DX v0.10 (src/pio.c). Changed: the jump pin (design D36).
// tests/test_pio_programs.py mirrors this state machine setup for its PIO simulator: keep both in sync

void on_transfer_program_init(PIO pio, uint sm, uint offset, uint clk_pin, uint cs_pin) {
    pio_sm_config c = on_transfer_program_get_default_config(offset);

    pio_gpio_init(pio, clk_pin);
    pio_gpio_init(pio, cs_pin);

    // CS keeps the pad's reset pull-down, as PicoBoot leaves it. A pull-up (1.0.0 to 1.1.0) made a
    // second DOL-001 count clocked extra transfers and fail; the clock-qualified count below handles
    // the false edge the pull-down lets through as the console first drives CS (design D36,
    // docs/research/cs-pull-up.md).

    // JMP PIN reads CS: a skipped transfer only counts if CLK rises while CS is still low (D36)
    sm_config_set_jmp_pin(&c, cs_pin);

    // Set CS as input
    pio_sm_set_consecutive_pindirs(pio, sm, cs_pin, 1, false);

    // Run at full system clock
    sm_config_set_clkdiv(&c, 1.f);

    // Load configuration and jump to start of the program
    pio_sm_init(pio, sm, offset, &c);
}

// The output state machine's set-up, for clocked_output and its length-based copy (D47)
static void output_program_init(PIO pio, uint sm, uint offset, pio_sm_config c, uint clk_pin, uint cs_pin,
                                uint data_pin) {
    pio_gpio_init(pio, clk_pin);
    pio_gpio_init(pio, cs_pin);
    pio_gpio_init(pio, data_pin);

    // Out and Set pins have to overlap so we can make line floating (Set it as input)
    sm_config_set_out_pins(&c, data_pin, 1);
    sm_config_set_set_pins(&c, data_pin, 1);

    // Set CLK and CS as inputs
    pio_sm_set_consecutive_pindirs(pio, sm, clk_pin, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm, cs_pin, 1, false);

    // Set DATA as input
    pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, false);

    // Shift left (MSB first), autopull with threshold 32
    sm_config_set_out_shift(&c, false, true, 32);
    // Join the TX and RX FIFOs into a single 8-word TX FIFO, disabling RX
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // Run at full system clock
    sm_config_set_clkdiv(&c, 1.f);

    // Load configuration and jump to start of the program
    pio_sm_init(pio, sm, offset, &c);
}

void clocked_output_program_init(PIO pio, uint sm, uint offset, uint clk_pin, uint cs_pin, uint data_pin) {
    output_program_init(pio, sm, offset, clocked_output_program_get_default_config(offset), clk_pin, cs_pin, data_pin);
}

// Length-based injection (designs D47, D48). tests/test_length_injection.py mirrors these two
// set-ups for its PIO simulator: keep both in sync

void clocked_output_length_program_init(PIO pio, uint sm, uint offset, uint clk_pin, uint cs_pin, uint data_pin) {
    // Starts at `first`, the program's first instruction
    output_program_init(pio, sm, offset, clocked_output_length_program_get_default_config(offset), clk_pin, cs_pin,
                        data_pin);
}

void length_detect_program_init(PIO pio, uint sm, uint offset, uint clk_pin, uint cs_pin) {
    pio_sm_config c = length_detect_program_get_default_config(offset);
    sm_config_set_jmp_pin(&c, cs_pin);          // CS rising before a 97th clock: not a block
    sm_config_set_in_pins(&c, clk_pin);         // mov y, pins reads CLK...
    sm_config_set_in_pin_count(&c, 1);          // ...alone (RP2350: IN_COUNT masks the rest)
    sm_config_set_clkdiv(&c, 1.f);              // Full system clock: it polls CLK every 3 cycles
    // No pio_gpio_init(): it only reads CS and CLK, whose pads the output program's set-up turns on
    pio_sm_init(pio, sm, offset, &c);
}

void pio_prepare_transfer(PIO pio, uint sm, uint32_t count, uint dest) {
    // Push the count into the SM’s TX FIFO
    pio_sm_put(pio, sm, count);
    // Pull it into OSR...
    pio_sm_exec(pio, sm, pio_encode_pull(true, true));
    // ...then move from OSR into register X or Y
    pio_sm_exec(pio, sm, pio_encode_mov(dest, pio_osr));
    // Drain the 32-bit count from OSR (to null) and prime OSR for autopull of the payload
    pio_sm_exec(pio, sm, pio_encode_out(pio_null, 32));
}
