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
#include "storage.h"
#include "wifi.h"
#include "api.h"
#include "updater.h"
#include "slots.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"

// The rest of flash is read through the untranslated XIP window: the loader maps only this
// firmware's slot at XIP_BASE (boot ROM research §4)
#define FLASH_UNTRANSLATED(offset) ((const void *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + (offset)))
#define PAYLOAD1 ((const uint32_t *)FLASH_UNTRANSLATED(LAYOUT_PAYLOAD1_OFFSET))
#define PAYLOAD2 ((const uint32_t *)FLASH_UNTRANSLATED(LAYOUT_PAYLOAD2_OFFSET))

#define FIRST_CS_TIMEOUT_US  (3 * 1000 * 1000)      // No CS by then: no console (bench)
#define INJECTION_TIMEOUT_US (10 * 1000 * 1000)     // Swiss (1.3 MB) takes about 0.4 s
#define WATCHDOG_MS          8000                   // A hang becomes a reboot the loader counts (D12)

#ifndef PICOBOOT_WIFI_TEST_CRASH
#define PICOBOOT_WIFI_TEST_CRASH 0                  // Test builds that crash on purpose (plan step 3.7)
#endif

// Bench debugging (CMake option PICOBOOT_WIFI_USB_TRACE): start-up steps over USB serial
#if PICOBOOT_WIFI_USB_TRACE
#define TRACE(...) printf("[%7lu ms] ", (unsigned long)(time_us_64() / 1000)), printf(__VA_ARGS__)
#else
#define TRACE(...) ((void)0)
#endif

// A fault reboots at once, as a crash: without BOOTINFO_CLEAN_REBOOT, so the loader counts it (D12)
void isr_hardfault(void) {
    watchdog_reboot(0, 0, 0);
    while (true) {}
}

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
        report.loader_flags = (uint8_t)(info & (BOOTINFO_FALLBACK | BOOTINFO_TRIAL | BOOTINFO_TRIAL_FAILED |
                                                BOOTINFO_CRASH_FALLBACK));
        report.loader_version = (uint8_t)((info & BOOTINFO_LOADER_MASK) >> BOOTINFO_LOADER_SHIFT);
        if (report.loader_version == 0) {
            report.loader_version = 1;      // Loaders before version 2 don't report one
        }
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
static injection_payload_t choose_payload(const bootctl_t *b) {
    injection_payload_t payload = {0};
    report.payload_chosen = b->active_payload;
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
static void inject(const injection_payload_t *payload, bool cs_pullup) {
    injection_prepare(payload, cs_pullup);
    counters_init();
    counters_start();           // The counters run before the injection, so they see every edge
    injection_go();

    const uint64_t t_ready = time_us_64();
    report.cs_high_at_ready = gpio_get(INJECTION_PIN_CS);
    report.main_to_ready_us = (uint32_t)(t_ready - t_main);
    report.margin_raw_us = report.margin_clocked_us = -1;

    while (!counters_first_raw_seen() && time_us_64() - t_ready < FIRST_CS_TIMEOUT_US) {
        watchdog_update();
    }

    if (counters_first_raw_seen()) {
        const uint64_t t_first = time_us_64();
        report.margin_raw_us = (int32_t)(t_first - t_ready);
        report.result = INJECTION_STALLED;
        while (time_us_64() - t_first < INJECTION_TIMEOUT_US) {
            watchdog_update();
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

// How fast the DMA reads payload 1 through the untranslated, uncached XIP window: the path the
// injection's DMA uses. Returns the microseconds for 64 KB (unpaced, into one word).
static uint32_t flash_dma_64k_us(void) {
    static uint32_t sink;
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    const uint64_t t0 = time_us_64();
    dma_channel_configure(ch, &c, &sink, PAYLOAD1, 65536 / 4, true);
    dma_channel_wait_for_finish_blocking(ch);
    const uint32_t us = (uint32_t)(time_us_64() - t0);
    dma_channel_unclaim(ch);
    return us;
}

// The access point with the report page, after the injection (design D9). Doesn't return.
// A firmware on trial that can't bring Wi-Fi up reboots as a crash (no clean-reboot word), so the
// loader goes back to the other slot (D12, the owner's choice in the phase 3 review). Off trial it
// carries on as before.
static void fail_trial(void) {
    if (report.loader_flags & BOOTINFO_TRIAL) {
        TRACE("on trial without Wi-Fi: failing the trial\n");
        watchdog_reboot(0, 0, 0);
        while (true) {}
    }
}

// D12: a firmware on trial that has done a complete power-on injection and brought Wi-Fi up keeps
// itself (the other way is the firmware page's Keep)
static void keep_if_proved(void) {
    if (report.power_on && report.result == INJECTION_COMPLETE) {
        bootctl_t b;
        storage_bootctl_load(&b);
        if (slots_keep(&b, report.slot)) {
            storage_bootctl_store(&b);
            TRACE("kept slot %c (D12)\n", report.slot == BOOTCTL_SLOT_B ? 'B' : 'A');
        }
    }
}

static void run_wifi(uint64_t t_start, uint32_t boot_number) {
    wifi_load();        // The settings, with an unconfirmed Wi-Fi change undone (design D41)

    static page_info_t page;
    page.report = &report;
    page.boot_number = boot_number;
    page.qmi_rcmd = qmi_hw->m[0].rcmd;
    page.qmi_rfmt = qmi_hw->m[0].rfmt;
    page.qmi_timing = qmi_hw->m[0].timing;
    page.sys_khz = clock_get_hz(clk_sys) / 1000;
    page.dma_64k_us = flash_dma_64k_us();
    page.wifi_area = wifiblob_select(report.slot);
    TRACE("Wi-Fi blob: area %d (-1 = none valid)\n", page.wifi_area);
    bool wifi_ran = false;
    if (page.wifi_area >= 0) {
        const uint64_t t_init = time_us_64();
        const int err = cyw43_arch_init_with_country(wifi_country());
        page.cyw43_init_ms = (uint32_t)((time_us_64() - t_init) / 1000);
        TRACE("cyw43_arch_init_with_country(%s): %d\n", wifi_settings()->country, err);
        if (err == 0) {
            const uint64_t t_net = time_us_64();
            // The LED after the injection (design D26)
            const wifi_led_t led = report.result == INJECTION_COMPLETE ? WIFI_LED_DONE :
                                   report.result == INJECTION_STALLED ? WIFI_LED_STALLED : WIFI_LED_STEADY;
            // Updates go into the other slot (D11), if the installed loader can start them
            updater_init(report.slot, report.loader_version, report.from_loader);
#if PICOBOOT_WIFI_TEST_BUILD
            const bool up = wifi_start(page_render, page_action, api_handle, &UPDATER_UPLOAD, led);  // With the test page
#else
            const bool up = wifi_start(NULL, NULL, api_handle, &UPDATER_UPLOAD, led);
#endif
            page.ap_start_ms = (uint32_t)((time_us_64() - t_net) / 1000);
            TRACE("wifi_start: %s, SSID %s, channel %u\n", up ? "ok" : "FAILED", wifi_ssid(), wifi_settings()->channel);
            if (!up) {
                fail_trial();           // On trial, a firmware without Wi-Fi is undone (D12)
            }
            {
                wifi_ran = true;
                page.ap_confirmed = up;
                page.wifi_up_ms = (uint32_t)((time_us_64() - t_start) / 1000);
                page_init(&page);
                api_init(&page);
                TRACE("Wi-Fi up after %lu ms; page at http://192.168.4.1/\n", (unsigned long)page.wifi_up_ms);
                if (up) {
                    keep_if_proved();   // D12: a complete power-on injection, then the AP really up
                }
                do {
                    net_poll();
                    watchdog_update();
#if PICOBOOT_WIFI_TEST_CRASH == 2
                    if (time_us_64() - t_start > 5 * 1000 * 1000) {
                        while (true) {}     // Hang: the watchdog resets, as a crash
                    }
#endif
                } while (wifi_poll());
                TRACE("Wi-Fi off: no client for %u minutes (design D20)\n", wifi_settings()->auto_off_minutes);
            }
        }
    }

    // A firmware on trial without a Wi-Fi blob, or whose CYW43 didn't initialise, is undone (D12)
    if (!wifi_ran) {
        fail_trial();
    }

    // Wi-Fi is off (D20), or there's no Wi-Fi blob, or Wi-Fi failed to start: idle until the next
    // power-on, sleeping between interrupts to draw less from the console's rail. Without the
    // watchdog: idling isn't a hang (phase 2 review).
    watchdog_disable();
    while (true) {
#if PICOBOOT_WIFI_USB_TRACE
        TRACE("idle: Wi-Fi is off\n");
        sleep_ms(5000);
#else
        __wfi();
#endif
    }
}

int main(void) {
    t_main = time_us_64();
#if PICOBOOT_WIFI_USB_TRACE
    stdio_init_all();           // Waits up to 10 s for a terminal (bench only: this delays the injection)
    TRACE("PicoBoot-WiFi %s, USB trace build\n", PICOBOOT_WIFI_BUILD);
#endif
    read_boot_info();
    watchdog_enable(WATCHDOG_MS, true);     // Fed in the injection's waits and the main loop
#if PICOBOOT_WIFI_TEST_CRASH == 1
    __builtin_trap();                       // A fault at start-up (plan step 3.7)
#endif
    TRACE("boot info: from loader %d, power-on %d, slot %c, fallback %d, defaults %d, CHIP_RESET 0x%08lx\n",
          report.from_loader, report.power_on, report.slot == BOOTCTL_SLOT_B ? 'B' : 'A', report.fallback,
          report.bootctl_default, (unsigned long)report.chip_reset);

    report.payload_chosen = BOOTCTL_NONE;
    if (report.power_on) {
        bootctl_t bootctl;
        storage_bootctl_load(&bootctl);
#if PICOBOOT_WIFI_TEST_BUILD
        report.cs_pulldown_test = bootctl.flags & BOOTCTL_FLAG_TEST_CS_PULLDOWN;
#endif
        const injection_payload_t payload = choose_payload(&bootctl);
        TRACE("payload %d, %lu words, CS %s\n", report.payload, (unsigned long)report.payload_words,
              report.cs_pulldown_test ? "pull-down (test)" : "pull-up");
        inject(&payload, !report.cs_pulldown_test);
        TRACE("injection result %d, margin %ld us, CS raw %lu clocked %lu\n", report.result,
              (long)report.margin_raw_us, (unsigned long)report.cs_raw, (unsigned long)report.cs_clocked);
    } else {
        report.result = INJECTION_NOT_RUN;      // A warm reboot: the console may be running (C2)
    }

    // The loader's news for boot control (D12): a failed trial of the other slot, or a takeover
    // after a crash loop. Flash may be written now, the injection is over.
    if (report.from_loader && report.loader_flags) {
        bootctl_t b;
        storage_bootctl_load(&b);
        if (slots_at_start(&b, report.slot, report.loader_flags)) {
            storage_bootctl_store(&b);
        }
    }

    report.chip_version = rp2350_chip_version();
    report.rom_version = rp2350_rom_version();

    // The injection has ended and Wi-Fi isn't running yet: nothing reads flash in the background
    const uint64_t t_end = time_us_64();
    const bool logged = storage_bootlog_append(&report);
    TRACE("boot log append: %s\n", logged ? "ok" : "FAILED");
    uint32_t boot_number = 0;
    if (logged) {
        boot_report_t newest;
        storage_bootlog_read(&newest, &boot_number, 1);
    }

    run_wifi(t_end, boot_number);
}
