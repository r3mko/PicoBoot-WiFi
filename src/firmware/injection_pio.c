/**
 * Copyright (c) 2025 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "injection_pio.h"
#include "injection.pio.h"

// From PicoBoot-DX v0.10 (src/pio.c). Changed: the CS pull-up and the jump pin (design D36).
// tests/test_pio_programs.py mirrors this state machine setup for its PIO simulator: keep both in sync

void on_transfer_program_init(PIO pio, uint sm, uint offset, uint clk_pin, uint cs_pin, bool cs_pullup) {
    pio_sm_config c = on_transfer_program_get_default_config(offset);

    pio_gpio_init(pio, clk_pin);
    pio_gpio_init(pio, cs_pin);

    // Pull CS up (design D36): the console leaves CS undriven for ~42 ms after power-on, and the
    // pad's reset pull-down would hold it low ("selected"), so a brief release by the console
    // would read as a CS pulse and shift the 224 skipped transfers by one.
    // The test mode (plan step 1.10) leaves the pad exactly as reset left it, as PicoBoot does:
    // switching the pull-up on and then back to the pull-down isn't the same on RP2350 A2, where
    // erratum E9 then holds the released pin at about 2.2 V, which reads high.
    if (cs_pullup) {
        gpio_pull_up(cs_pin);
    }

    // JMP PIN reads CS: a skipped transfer only counts if CLK rises while CS is still low (D36)
    sm_config_set_jmp_pin(&c, cs_pin);

    // Set CS as input
    pio_sm_set_consecutive_pindirs(pio, sm, cs_pin, 1, false);

    // Run at full system clock
    sm_config_set_clkdiv(&c, 1.f);

    // Load configuration and jump to start of the program
    pio_sm_init(pio, sm, offset, &c);
}

void clocked_output_program_init(PIO pio, uint sm, uint offset, uint clk_pin, uint cs_pin, uint data_pin) {
    pio_sm_config c = clocked_output_program_get_default_config(offset);

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
