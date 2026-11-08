/**
 * Copyright (c) 2022 Maciej Kobus
 * Copyright (c) 2025 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// PicoBoot-WiFi phase 0 measurement build (implementation plan step 0.1).
//
// This is PicoBoot-DX v0.10's main() for the Pico 2 W. The path up to "PIO ready" is
// unchanged, apart from reading two reset registers and starting a passive CS counter
// in pio1 (design D31). After that, instead of PicoBoot-DX's LED blinking:
//   - it timestamps the console's first CS and waits for the injection to end;
//   - it writes the measurements to a boot log at the end of flash;
//   - only then it starts Wi-Fi (design D9) with a report page (design D32).
// Lines that differ from PicoBoot-DX are marked "phase 0".

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/powman.h"   // phase 0
#include "hardware/structs/watchdog.h" // phase 0
#include "pico/cyw43_arch.h"           // phase 0
#include "pico/unique_id.h"            // phase 0

#include "pio.h"
#include "picoboot-dx.pio.h"
#include "ipl.h"
#include "endian.h"
#include "measure.h"                   // phase 0
#include "bootlog.h"                   // phase 0
#include "report.h"                    // phase 0
#include "net.h"                       // phase 0

#define PAYLOAD_INVALID SIZE_MAX

extern const uint32_t flash_payload[];
extern const uint32_t flash_payload_end[];

static const uint PIN_CS = 4;             // U10 chip select
static const uint PIN_CLK = 5;            // EXI bus clock line
static const uint PIN_DATA = 6;           // Data pin used for output

static const uint BOOST_CLOCK = 250000;   // Set 250MHz clock to get more cycles.

// phase 0: how long to wait for the console
static const uint64_t FIRST_CS_TIMEOUT_US = 3 * 1000 * 1000;    // No CS by then: no console (bench)
static const uint64_t INJECTION_TIMEOUT_US = 10 * 1000 * 1000;  // Swiss (1.3 MB) takes well under 2 s

static const char *AP_PASSWORD = "picoboot";                    // phase 0: design D30

static int dma_chan;

static bool dma_busy(void) {
    return dma_channel_is_busy(dma_chan);
}

// Validate payload stored in flash.
// The payload must contain a (big-endian) header and footer with the following layout:
//   [0] = magic 0 ("IPLB")
//   [1] = magic 1 ("OOT ")
//   [2] = size of payload in bytes
//   ...payload data...
//   [word_count - 1] = magic 2 ("PICO")
static size_t validate_payload(void) {
    const uint32_t *p = flash_payload;

    if (BigEndian32(p[0]) != 0x49504C42 ||   // "IPLB"
        BigEndian32(p[1]) != 0x4F4F5420) {   // "OOT "
        return PAYLOAD_INVALID;
    }

    uint32_t raw_size_bytes = BigEndian32(p[2]);

    // Size includes the 32-byte header and 4-byte footer, and must fit in the payload region
    if (raw_size_bytes < 36 ||
        raw_size_bytes > (uintptr_t)flash_payload_end - (uintptr_t)flash_payload) {
        return PAYLOAD_INVALID;
    }

    const size_t word_count = raw_size_bytes / sizeof(*p);
    const size_t alignment = 1024 / sizeof(*p);
    const size_t word_count_aligned = (word_count + alignment - 1) / alignment * alignment;

    if (&p[word_count_aligned] > flash_payload_end) {
        return PAYLOAD_INVALID;
    }

    if (BigEndian32(p[word_count - 1]) != 0x5049434F) {   // "PICO"
        return PAYLOAD_INVALID;
    }

    return word_count_aligned;
}

int main(void) {
    // phase 0: when main() starts and why the chip was reset (one register read each)
    const uint64_t t_main = time_us_64();
    boot_record_t rec = {0};
    rec.chip_reset = powman_hw->chip_reset;
    rec.watchdog_reason = watchdog_hw->reason;

    // Set 250MHz clock to get more cycles in between CLK pulses.
    // This is the lowest value I was able to make the code work.
    // Should be still considered safe for most Pico boards.
    set_sys_clock_khz(BOOST_CLOCK, true);

    // Prioritize DMA engine as it does the most work
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    gpio_set_slew_rate(PIN_DATA, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_DATA, GPIO_DRIVE_STRENGTH_8MA);

    const PIO pio = pio0;

    //
    // State Machine: Transfer Start
    //
    // Counts all consecutive transfers and sets IRQ
    // when first 1 kilobyte transfer starts.
    //

    uint transfer_start_sm = pio_claim_unused_sm(pio, true);
    uint transfer_start_offset = pio_add_program(pio, &on_transfer_program);

    on_transfer_program_init(pio, transfer_start_sm, transfer_start_offset, PIN_CLK, PIN_CS);
    // Wait for 224 CS pulses before firing the IRQ
    // this marks the boundary where the first 1 kB (1024 bytes) transfer is about to start
    pio_prepare_transfer(pio, transfer_start_sm, 224, pio_x);

    //
    // State Machine: Clocked Output
    //
    // It waits for IRQ signal from first SM and samples clock signal
    // to output IPL data bits.
    //

    uint clocked_output_sm = pio_claim_unused_sm(pio, true);
    uint clocked_output_offset = pio_add_program(pio, &clocked_output_program);

    clocked_output_program_init(pio, clocked_output_sm, clocked_output_offset, PIN_CLK, PIN_CS, PIN_DATA);
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

    // Determine which payload to use: flash or default IPL.
    // If valid flash payload is found, use it and enable byte-swap for big-endian format.
    // Otherwise, default to built-in IPL array (already in correct byte order).
    size_t payload_word_count = validate_payload();
    const uint32_t *payload;

    // phase 0: the report page can switch to the built-in IPL for the next boots (two flash reads)
    const uint32_t switches = bootlog_switches();
    if (switches & SWITCH_FORCE_BUILTIN) {
        payload_word_count = PAYLOAD_INVALID;
    }

    if (payload_word_count != PAYLOAD_INVALID) {
        payload = flash_payload;
        channel_config_set_bswap(&c, true);
        rec.flags |= BOOT_FLAG_FLASH_PAYLOAD;   // phase 0
    } else {
        payload = ipl;
        payload_word_count = count_of(ipl);
    }

    dma_channel_configure(
        dma_chan,                       // DMA channel
        &c,                             // Config
        &pio->txf[clocked_output_sm],   // Dest: PIO TX FIFO
        payload,                        // Source: IPL array or flash_payload
        payload_word_count,             // Number of words
        true                            // Start immediately
    );

    // phase 0: passive CS counter in pio1, running before the injection state machines start
    cs_counter_init();
    cs_counter_start();

    // phase 0: never inject after a reboot without a power reset (watchdog, BOOTSEL, debugger):
    // the console may already be running (design C2). Brown-outs still inject, so a cold boot
    // that shows up as BOR instead of POR is still measured; the log shows which it was.
    const bool power_reset = rec.chip_reset & (POWMAN_CHIP_RESET_HAD_POR_BITS |
                                               POWMAN_CHIP_RESET_HAD_BOR_BITS |
                                               POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS);
    if (power_reset) {
        // Start PIO state machines
        pio_sm_set_enabled(pio, transfer_start_sm, true);
        pio_sm_set_enabled(pio, clocked_output_sm, true);
    } else {
        rec.flags |= BOOT_FLAG_SKIPPED;
        dma_channel_abort(dma_chan);
    }

    // phase 0: from here on PicoBoot-DX would start the CYW43 for its LED and blink while
    // the DMA is busy. This build measures instead and starts Wi-Fi only afterwards (D9).
    const uint64_t t_ready = time_us_64();
    rec.timer_at_main_us = (uint32_t)t_main;
    rec.main_to_ready_us = (uint32_t)(t_ready - t_main);
    rec.payload_words = (uint32_t)payload_word_count;
    rec.margin_us = -1;

    while (power_reset && !cs_counter_first_edge_seen() && time_us_64() - t_ready < FIRST_CS_TIMEOUT_US) {
        tight_loop_contents();
    }
    const uint64_t t_first_cs = time_us_64();
    bool complete = false;

    if (power_reset && cs_counter_first_edge_seen()) {
        rec.flags |= BOOT_FLAG_FIRST_CS;
        rec.margin_us = (int32_t)(t_first_cs - t_ready);

        // The DMA is done once the last word is in the PIO FIFO, wait until the PIO
        // has also shifted it out and released the data line (disable_chip)
        while (time_us_64() - t_first_cs < INJECTION_TIMEOUT_US) {
            if (!dma_busy() &&
                pio_sm_get_pc(pio, clocked_output_sm) == clocked_output_offset + clocked_output_offset_disable_chip) {
                complete = true;
                break;
            }
        }
        rec.flags |= complete ? BOOT_FLAG_COMPLETE : BOOT_FLAG_STALLED;
    }
    const uint64_t t_end = time_us_64();
    rec.injection_us = (rec.flags & BOOT_FLAG_FIRST_CS) ? (uint32_t)(t_end - t_first_cs) : 0;
    rec.cs_at_end = cs_counter_read();
    rec.words_delivered = (uint32_t)payload_word_count -
                          (dma_channel_hw_addr(dma_chan)->transfer_count & DMA_CH0_TRANS_COUNT_COUNT_BITS);

    if (!complete) {
        // phase 0: no console, or a stall: stop feeding and make sure the data line is released
        dma_channel_abort(dma_chan);
        pio_sm_set_enabled(pio, clocked_output_sm, false);
        pio_sm_set_consecutive_pindirs(pio, clocked_output_sm, PIN_DATA, 1, false);
    }

    // Lower the clock to 125MHz (RP2040 default), the boost is no longer needed
    set_sys_clock_khz((BOOST_CLOCK / 2), true);

    // Stop PIO state machines
    pio_sm_set_enabled(pio, transfer_start_sm, false);
    pio_sm_set_enabled(pio, clocked_output_sm, false);

    // phase 0: the injection is over and nothing reads flash in the background: log this boot
    bootlog_append(&rec);

    // phase 0: one-shot switch for the current measurement with the radio still off
    if (switches & SWITCH_WIFI_LATE) {
        bootlog_set_switches(switches & ~SWITCH_WIFI_LATE);
        sleep_ms(60 * 1000);
    }

    // phase 0: Wi-Fi with the report page (D32); SSID from the chip's unique ID (D30)
    static char ssid[32];
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    snprintf(ssid, sizeof(ssid), "PicoBoot-Measure-%02X%02X",
             id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2], id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1]);

    report_info_t info = {
        .boot = rec,
        .injection_end_us = t_end,
        .ssid = ssid,
        .password = AP_PASSWORD,
    };

    if (cyw43_arch_init() == 0 && net_start(ssid, AP_PASSWORD)) {
        info.wifi_up_us = time_us_64();
        report_init(&info);

        // LED: on while the AP is up; blinks fast after a stall, slowly without a console
        uint32_t blink_ms = (rec.flags & BOOT_FLAG_STALLED) ? 125 : (complete ? 0 : 1000);
        int led_state = -1;
        while (true) {
            net_poll();
            bool led = blink_ms == 0 || (to_ms_since_boot(get_absolute_time()) / blink_ms) % 2 == 0;
            if (led != led_state) {     // Each LED change is an SPI transaction to the CYW43
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
                led_state = led;
            }
        }
    }

    // Wi-Fi failed to start: idle, as PicoBoot-DX does
    while (true) {
        tight_loop_contents();
    }
}
