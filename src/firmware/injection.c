/**
 * Copyright (c) 2022 Maciej Kobus
 * Copyright (c) 2025-2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The injection, from PicoBoot-DX v0.10's main() (src/picoboot-dx.c), split into start,
// check and finish so the firmware can measure and decide around it.

#include "injection.h"
#include "injection_pio.h"
#include "injection.pio.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"

#ifndef PICOBOOT_WIFI_INJECTION_KHZ
#error "PICOBOOT_WIFI_INJECTION_KHZ (the clock during the injection) comes from CMakeLists.txt"
#endif
static const uint BOOST_CLOCK = PICOBOOT_WIFI_INJECTION_KHZ;   // More cycles per console clock (design D27)

static const PIO pio = pio0;
static uint transfer_start_sm, clocked_output_sm, clocked_output_offset;
static int dma_chan = -1;

void injection_prepare(const injection_payload_t *payload, bool cs_pullup) {
    // Raise the clock (250MHz by default) to get more cycles in between CLK pulses.
    // This is the lowest value I was able to make the code work.
    // Should be still considered safe for most Pico boards.
    set_sys_clock_khz(BOOST_CLOCK, true);

    // Prioritize DMA engine as it does the most work
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    gpio_set_slew_rate(INJECTION_PIN_DATA, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(INJECTION_PIN_DATA, GPIO_DRIVE_STRENGTH_8MA);

    //
    // State Machine: Transfer Start
    //
    // Counts all consecutive transfers and sets IRQ
    // when first 1 kilobyte transfer starts.
    //

    transfer_start_sm = pio_claim_unused_sm(pio, true);
    uint transfer_start_offset = pio_add_program(pio, &on_transfer_program);

    on_transfer_program_init(pio, transfer_start_sm, transfer_start_offset, INJECTION_PIN_CLK, INJECTION_PIN_CS,
                             cs_pullup);
    // Wait for 224 CS pulses before firing the IRQ
    // this marks the boundary where the first 1 kB (1024 bytes) transfer is about to start
    pio_prepare_transfer(pio, transfer_start_sm, 224, pio_x);

    //
    // State Machine: Clocked Output
    //
    // It waits for IRQ signal from first SM and samples clock signal
    // to output IPL data bits.
    //

    clocked_output_sm = pio_claim_unused_sm(pio, true);
    clocked_output_offset = pio_add_program(pio, &clocked_output_program);

    clocked_output_program_init(pio, clocked_output_sm, clocked_output_offset, INJECTION_PIN_CLK, INJECTION_PIN_CS, INJECTION_PIN_DATA);
    // Set up the clocked‐output SM to shift out 8192 bits (1024 bytes)
    // using 8191 here because the PIO countdown is zero-based
    pio_prepare_transfer(pio, clocked_output_sm, 8191, pio_y);

    //
    // Direct Memory Access: Channel Setup
    //
    // Set up DMA for reading IPL to PIO TX FIFO
    //

    dma_chan = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    // Set transfer size to 32 bits
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    // Read address increments (for IPL array)
    channel_config_set_read_increment(&c, true);
    // Write address fixed (TX FIFO register)
    channel_config_set_write_increment(&c, false);
    // Use PIO TX FIFO as DMA trigger
    channel_config_set_dreq(&c, pio_get_dreq(pio, clocked_output_sm, true));
    // Flash payloads are stored big-endian
    channel_config_set_bswap(&c, payload->byteswap);

    dma_channel_configure(
        dma_chan,                       // DMA channel
        &c,                             // Config
        &pio->txf[clocked_output_sm],   // Dest: PIO TX FIFO
        payload->words,                 // Source: built-in IPL or flash payload
        payload->count,                 // Number of words
        true                            // Start immediately
    );
}

void injection_go(void) {
    // Start PIO state machines
    pio_sm_set_enabled(pio, transfer_start_sm, true);
    pio_sm_set_enabled(pio, clocked_output_sm, true);
}

bool injection_complete(void) {
    // The DMA is done once the last word is in the PIO FIFO, wait until the PIO
    // has also shifted it out and released the data line (disable_chip)
    return !dma_channel_is_busy(dma_chan) &&
           pio_sm_get_pc(pio, clocked_output_sm) == clocked_output_offset + clocked_output_offset_disable_chip;
}

uint32_t injection_words_left(void) {
    return dma_channel_hw_addr(dma_chan)->transfer_count & DMA_CH0_TRANS_COUNT_COUNT_BITS;
}

void injection_finish(void) {
    if (!injection_complete()) {
        // Stop feeding (clear EN first: RP2350-E5) and make sure the data line is released
        hw_clear_bits(&dma_hw->ch[dma_chan].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
        dma_channel_abort(dma_chan);
        pio_sm_set_enabled(pio, clocked_output_sm, false);
        pio_sm_set_consecutive_pindirs(pio, clocked_output_sm, INJECTION_PIN_DATA, 1, false);
    }

    // Back to the SDK's clock (150MHz), the same as on warm boots: the boost is no longer needed
    set_sys_clock_khz(SYS_CLK_KHZ, true);

    // Stop PIO state machines
    pio_sm_set_enabled(pio, transfer_start_sm, false);
    pio_sm_set_enabled(pio, clocked_output_sm, false);
}
