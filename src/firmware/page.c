/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdarg.h>
#include <stdio.h>
#include "page.h"
#include "net.h"
#include "bootctl.h"
#include "hardware/adc.h"
#include "hardware/structs/powman.h"
#include "pico/time.h"

#define SKIPPED_TRANSFERS 224
#define WORDS_PER_TRANSFER 256

static page_info_t info;

typedef struct {
    char *buf;
    size_t size, len;
} out_t;

static void put(out_t *o, const char *fmt, ...) {
    if (o->len + 1 >= o->size) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o->buf + o->len, o->size - o->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        o->len += (size_t)n;
        if (o->len > o->size - 1) {
            o->len = o->size - 1;
        }
    }
}

void page_init(const page_info_t *i) {
    info = *i;
    adc_init();
    adc_set_temp_sensor_enabled(true);
}

static float temperature_c(void) {
    adc_select_input(NUM_ADC_CHANNELS - 1);     // The temperature sensor is the last ADC input
    float volts = adc_read() * 3.3f / 4096.0f;
    return 27.0f - (volts - 0.706f) / 0.001721f;
}

static const char *reset_reason(uint32_t chip_reset) {
    if (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) return "POR";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS) return "BOR";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) return "RUN";
    if (chip_reset & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
                      POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS)) {
        return "WDOG";
    }
    return "other";
}

static const char *payload_name(payload_source_t p) {
    switch (p) {
        case PAYLOAD_SOURCE_PAYLOAD1: return "payload 1";
        case PAYLOAD_SOURCE_PAYLOAD2: return "payload 2";
        case PAYLOAD_SOURCE_BUILTIN: return "built-in gekkoboot";
        default: return "none";
    }
}

static const char *result_name(injection_result_t r) {
    switch (r) {
        case INJECTION_COMPLETE: return "complete";
        case INJECTION_STALLED: return "STALLED";
        case INJECTION_NO_CS: return "no CS seen (no console?)";
        default: return "not run: a warm boot (design D8)";
    }
}

// The CS counts against what the payload needs: 224 skipped transfers plus one per 1K, and the
// console's next access right after the payload usually adds one more
static void put_counts(out_t *o, const boot_report_t *r) {
    const uint32_t expected = SKIPPED_TRANSFERS + r->payload_words / WORDS_PER_TRANSFER;
    put(o, "  CS at the end      : raw %lu, clocked %lu, expected %lu or %lu",
        (unsigned long)r->cs_raw, (unsigned long)r->cs_clocked, (unsigned long)expected, (unsigned long)(expected + 1));
    if (r->cs_raw > r->cs_clocked) {
        put(o, ": GLITCH, %lu CS pulse(s) without a transfer, ignored by the injection (D36)\n",
            (unsigned long)(r->cs_raw - r->cs_clocked));
    } else if (r->cs_clocked < expected) {
        put(o, ": MISSED %lu transfer(s)\n", (unsigned long)(expected - r->cs_clocked));
    } else {
        put(o, ": ok\n");
    }
}

size_t page_render(char *buf, size_t size) {
    out_t o = {buf, size, 0};
    const boot_report_t *r = info.report;
    const uint32_t up_s = (uint32_t)(time_us_64() / 1000000);

    put(&o, "<!doctype html><html><head><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>PicoBoot-WiFi</title>"
            "<style>body{font-family:monospace;margin:12px}pre{white-space:pre-wrap}"
            "form,a{display:inline-block;margin:6px 12px 6px 0}</style></head><body><pre>");
    put(&o, "PicoBoot-WiFi %s (phase 1 test build)\n", PICOBOOT_WIFI_BUILD);
    put(&o, "SSID: %s   Password: %s\n\n", info.ssid, info.password);

    put(&o, "THIS BOOT\n");
    if (r->from_loader) {
        put(&o, "  Started by         : the loader, slot %c%s; boot control %s\n", r->slot == BOOTCTL_SLOT_B ? 'B' : 'A',
            r->fallback ? " (fallback: the first choice held no image)" : "",
            r->bootctl_default ? "empty (defaults)" : "record");
    } else {
        put(&o, "  Started by         : no loader (firmware installed on its own)\n");
    }
    put(&o, "  Reset              : %s (CHIP_RESET 0x%08lx), power-on: %s\n", reset_reason(r->chip_reset),
        (unsigned long)r->chip_reset, r->power_on ? "yes" : "no (warm)");
    put(&o, "  RP2350             : stepping A%u, boot ROM version %u\n", r->chip_version, r->rom_version);
    put(&o, "  Injection          : %s\n", result_name(r->result));
    if (r->result != INJECTION_NOT_RUN) {
        put(&o, "  Payload            : %s, %lu transfers (%lu bytes), %lu words delivered\n", payload_name(r->payload),
            (unsigned long)(r->payload_words / WORDS_PER_TRANSFER), (unsigned long)(r->payload_words * 4),
            (unsigned long)r->words_delivered);
        put(&o, "  main() to PIO ready: %lu us; CS at PIO ready: %s\n", (unsigned long)r->main_to_ready_us,
            r->cs_high_at_ready ? "high" : "LOW");
    }
    if (r->result == INJECTION_COMPLETE || r->result == INJECTION_STALLED) {
        put(&o, "  MARGIN             : %ld us from PIO ready to the first CS; first real transfer at %ld us\n",
            (long)r->margin_raw_us, (long)r->margin_clocked_us);
        put(&o, "  Injection time     : %lu ms from the first CS\n", (unsigned long)(r->injection_us / 1000));
        put_counts(&o, r);
    }

    put(&o, "\nNOW\n");
    if (info.wifi_area >= 0) {
        put(&o, "  Wi-Fi              : blob from Wi-Fi %c, AP up %lu ms after the injection\n",
            info.wifi_area == BOOTCTL_SLOT_B ? 'B' : 'A', (unsigned long)info.wifi_up_ms);
    }
    put(&o, "  Uptime             : %02lu:%02lu:%02lu\n", (unsigned long)(up_s / 3600),
        (unsigned long)(up_s / 60 % 60), (unsigned long)(up_s % 60));
    put(&o, "  Temperature        : %.1f C (RP2350 sensor; reads low on a low 3.3 V rail)\n", (double)temperature_c());
    put(&o, "  Wi-Fi clients      : %d\n", net_client_count());

    put(&o, "</pre>"
            "<form method=post action=/reboot-wd><button>Reboot (watchdog)</button></form>"
            "<form method=post action=/reboot-rom><button>Reboot (ROM)</button></form>"
            "<a href=\"/\">Refresh</a></body></html>\n");
    return o.len;
}
