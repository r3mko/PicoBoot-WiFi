/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The firmware, started by the loader from slot A or B (design D7).
//
// At a power-on it injects the chosen payload, measured into the boot report (D31); after a warm
// reboot it doesn't (D8). Then it starts Wi-Fi with the report page (D9, D32).

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/platform.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/powman.h"
#include "layout.h"
#include "records.h"
#include "bootctl.h"
#include "bootinfo.h"
#include "ipl.h"
#include "payload.h"
#include "injection.h"
#include "counters.h"
#include "report.h"
#include "wifiblob.h"
#include "net.h"
#include "page.h"

// The rest of flash is read through the untranslated XIP window: the loader maps only this
// firmware's slot at XIP_BASE (boot ROM research §4)
#define FLASH_UNTRANSLATED(offset) ((const void *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + (offset)))
#define PAYLOAD1 ((const uint32_t *)FLASH_UNTRANSLATED(LAYOUT_PAYLOAD1_OFFSET))
#define PAYLOAD2 ((const uint32_t *)FLASH_UNTRANSLATED(LAYOUT_PAYLOAD2_OFFSET))

#define FIRST_CS_TIMEOUT_US  (3 * 1000 * 1000)      // No CS by then: no console (bench)
#define INJECTION_TIMEOUT_US (10 * 1000 * 1000)     // Swiss (1.3 MB) takes about 0.4 s

static const char *AP_PASSWORD = "picoboot";        // Design D30: documented default

static boot_report_t report;
static uint64_t t_main;

// The loader's decision (POWMAN scratch). Without a loader (the firmware installed on its own at
// offset 0), the firmware applies design D8's rule itself.
static void read_boot_info(void) {
    const uint32_t info = powman_hw->scratch[BOOTINFO_SCRATCH_INFO];
    if (bootinfo_valid(info)) {
        report.from_loader = true;
        report.power_on = info & BOOTINFO_POWER_ON;
        report.slot = (info & BOOTINFO_SLOT_B) ? BOOTCTL_SLOT_B : BOOTCTL_SLOT_A;
        report.fallback = info & BOOTINFO_FALLBACK;
        report.bootctl_default = info & BOOTINFO_BOOTCTL_DEFAULT;
        report.chip_reset = powman_hw->scratch[BOOTINFO_SCRATCH_CHIP_RESET];
        // Consumed: a later boot without a loader mustn't take this for its own
        powman_hw->scratch[BOOTINFO_SCRATCH_INFO] = 0;
    } else {
        report.chip_reset = powman_hw->chip_reset;
        report.power_on = (report.chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) &&
                          powman_hw->scratch[BOOTINFO_SCRATCH_MARKER] != BOOTINFO_SESSION_MARKER;
        powman_hw->scratch[BOOTINFO_SCRATCH_MARKER] = BOOTINFO_SESSION_MARKER;
        report.slot = BOOTCTL_SLOT_A;
    }
}

// The payload boot control asks for (design D15), or the built-in IPL if that one isn't valid
static injection_payload_t choose_payload(void) {
    const record_header_t *rec = record_find(FLASH_UNTRANSLATED(LAYOUT_BOOTCTL_OFFSET), BOOTCTL_MAGIC,
                                             BOOTCTL_VERSION, sizeof(bootctl_t), NULL);
    const bootctl_t *b = rec ? record_payload(rec) : &BOOTCTL_DEFAULT;

    injection_payload_t payload = {0};
    if (b->active_payload == BOOTCTL_PAYLOAD_1) {
        payload = (injection_payload_t){PAYLOAD1, payload_check(PAYLOAD1, LAYOUT_PAYLOAD1_SIZE), true};
        report.payload = PAYLOAD_SOURCE_PAYLOAD1;
    } else if (b->active_payload == BOOTCTL_PAYLOAD_2) {
        payload = (injection_payload_t){PAYLOAD2, payload_check(PAYLOAD2, LAYOUT_PAYLOAD2_SIZE), true};
        report.payload = PAYLOAD_SOURCE_PAYLOAD2;
    }
    if (payload.count == PAYLOAD_INVALID) {
        payload = (injection_payload_t){ipl, count_of(ipl), false};
        report.payload = PAYLOAD_SOURCE_BUILTIN;
    }
    report.payload_words = (uint32_t)payload.count;
    return payload;
}

// Runs the injection and measures it into the report. Nothing slow may run before "PIO ready",
// and nothing may write flash until the injection has ended (design C1, C6).
static void inject(const injection_payload_t *payload) {
    injection_prepare(payload);
    counters_init();
    counters_start();           // The counters run before the injection, so they see every edge
    injection_go();

    const uint64_t t_ready = time_us_64();
    report.cs_high_at_ready = gpio_get(INJECTION_PIN_CS);
    report.main_to_ready_us = (uint32_t)(t_ready - t_main);
    report.margin_raw_us = report.margin_clocked_us = -1;

    while (!counters_first_raw_seen() && time_us_64() - t_ready < FIRST_CS_TIMEOUT_US) {
        tight_loop_contents();
    }

    if (counters_first_raw_seen()) {
        const uint64_t t_first = time_us_64();
        report.margin_raw_us = (int32_t)(t_first - t_ready);
        report.result = INJECTION_STALLED;
        while (time_us_64() - t_first < INJECTION_TIMEOUT_US) {
            if (report.margin_clocked_us < 0 && counters_first_clocked_seen()) {
                report.margin_clocked_us = (int32_t)(time_us_64() - t_ready);
            }
            if (injection_complete()) {
                report.result = INJECTION_COMPLETE;
                break;
            }
        }
        report.injection_us = (uint32_t)(time_us_64() - t_first);
    } else {
        report.result = INJECTION_NO_CS;
    }

    counters_snapshot(&report.cs_raw, &report.cs_clocked);
    report.words_delivered = (uint32_t)(payload->count - injection_words_left());
    injection_finish();
}

// The access point with the report page, after the injection (design D9). Doesn't return.
static void run_wifi(uint64_t t_start) {
    // SSID from the last four hex digits of the chip's unique ID (design D30)
    static char ssid[16];
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    snprintf(ssid, sizeof(ssid), "PicoBoot-%02X%02X",
             id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2], id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1]);

    page_info_t page = {.report = &report, .ssid = ssid, .password = AP_PASSWORD};
    page.wifi_area = wifiblob_select(report.slot);
    if (page.wifi_area >= 0 && cyw43_arch_init() == 0 && net_start(ssid, AP_PASSWORD, page_render)) {
        page.wifi_up_ms = (uint32_t)((time_us_64() - t_start) / 1000);
        page_init(&page);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
        while (true) {
            net_poll();
        }
    }

    // No Wi-Fi blob, or Wi-Fi failed to start: idle, as PicoBoot-DX does
    while (true) {
        tight_loop_contents();
    }
}

int main(void) {
    t_main = time_us_64();
    read_boot_info();

    if (report.power_on) {
        const injection_payload_t payload = choose_payload();
        inject(&payload);
    } else {
        report.result = INJECTION_NOT_RUN;      // A warm reboot: the console may be running (C2)
    }

    report.chip_version = rp2350_chip_version();
    report.rom_version = rp2350_rom_version();

    run_wifi(time_us_64());
}
