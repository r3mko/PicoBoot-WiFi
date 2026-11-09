/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "page.h"
#include "net.h"
#include "bootctl.h"
#include "storage.h"
#include "settings.h"
#include "wifi.h"
#include "hardware/adc.h"
#include "hardware/structs/powman.h"
#include "pico/time.h"

#define SKIPPED_TRANSFERS 224
#define WORDS_PER_TRANSFER 256

static page_info_t info;
static boot_report_t log_rows[STORAGE_BOOTLOG_ENTRIES];
static uint32_t log_seq[STORAGE_BOOTLOG_ENTRIES];

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

float page_temperature_c(void) {
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

static const char *short_result(const boot_report_t *r) {
    const uint32_t expected = SKIPPED_TRANSFERS + r->payload_words / WORDS_PER_TRANSFER;
    switch (r->result) {
        case INJECTION_NOT_RUN: return "warm (not run)";
        case INJECTION_NO_CS: return "no CS";
        case INJECTION_STALLED: return "STALLED";
        default: break;
    }
    if (r->cs_raw > r->cs_clocked) return "ok, GLITCH survived";
    if (r->cs_clocked < expected) return "MISSED";
    return "ok";
}

// The last boots from the boot log (design D39), newest first
static void put_log(out_t *o) {
    const unsigned n = storage_bootlog_read(log_rows, log_seq, STORAGE_BOOTLOG_ENTRIES);
    put(o, "\nBOOT LOG (newest first, %u kept)\n", n);
    put(o, "  boot slot reset payload   xfers  margin_us  raw/clocked  CS@ready      result\n");
    for (unsigned i = 0; i < n; i++) {
        const boot_report_t *r = &log_rows[i];
        const bool ran = r->result == INJECTION_COMPLETE || r->result == INJECTION_STALLED;
        char counts[24] = "-", margin[12] = "-", cs[16] = "-";
        if (ran) {
            snprintf(counts, sizeof(counts), "%lu/%lu", (unsigned long)r->cs_raw, (unsigned long)r->cs_clocked);
            // The real margin: to the first clocked transfer, which a glitch doesn't move
            snprintf(margin, sizeof(margin), "%ld",
                     (long)(r->margin_clocked_us >= 0 ? r->margin_clocked_us : r->margin_raw_us));
        }
        if (r->result != INJECTION_NOT_RUN) {
            snprintf(cs, sizeof(cs), "%s%s", r->cs_high_at_ready ? "high" : "low", r->cs_pulldown_test ? " (pd)" : "");
        }
        put(o, "  %4lu  %c   %-5s %-9s %5lu  %9s  %11s  %-12s  %s\n", (unsigned long)log_seq[i],
            r->slot == BOOTCTL_SLOT_B ? 'B' : 'A', r->power_on ? reset_reason(r->chip_reset) : "warm",
            r->result == INJECTION_NOT_RUN ? "-" : (r->payload == PAYLOAD_SOURCE_BUILTIN ? "built-in" :
                                                    r->payload == PAYLOAD_SOURCE_PAYLOAD2 ? "payload 2" : "payload 1"),
            (unsigned long)(r->payload_words / WORDS_PER_TRANSFER), margin, counts, cs, short_result(r));
    }
}

// Set when the last button's flash write failed (shown on the page until one succeeds)
static bool store_failed;

bool page_action(const char *path) {
    bootctl_t b;
    storage_bootctl_load(&b);
    if (strcmp(path, "/payload1") == 0) {
        b.active_payload = BOOTCTL_PAYLOAD_1;
    } else if (strcmp(path, "/payload2") == 0) {
        b.active_payload = BOOTCTL_PAYLOAD_2;
    } else if (strcmp(path, "/builtin") == 0) {
        b.active_payload = BOOTCTL_PAYLOAD_BUILTIN;
    } else if (strcmp(path, "/cs-pullup") == 0) {
        b.flags &= (uint8_t)~BOOTCTL_FLAG_TEST_CS_PULLDOWN;
    } else if (strcmp(path, "/cs-pulldown") == 0) {
        b.flags |= BOOTCTL_FLAG_TEST_CS_PULLDOWN;
    } else {
        return false;
    }
    store_failed = !storage_bootctl_store(&b);
    return true;
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

static void put_settings(out_t *o, const char *label, const settings_t *s, bool stored) {
    char off[16] = "never";
    if (s->auto_off_minutes != SETTINGS_AUTO_OFF_NEVER) {
        snprintf(off, sizeof(off), "%u min", s->auto_off_minutes);
    }
    put(o, "  %-19s: channel %u, country %s, Tx power %s, Wi-Fi off after %s, LED %s%s\n", label,
        s->channel, s->country, SETTINGS_TX_NAMES[s->tx_power], off, s->led ? "on" : "off",
        stored ? "" : " (settings empty: defaults)");
    const int q = wifi_tx_power_qdbm();
    if (q >= 0) {
        put(o, "  Tx power (chip)    : %d.%02d dBm, as the CYW43 reports it\n", q / 4, q % 4 * 25);
    }
}

static void html_escape(char *out, size_t size, const char *s) {
    size_t n = 0;
    for (; *s && n + 7 < size; s++) {
        const char *e = *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;" :
                        *s == '\'' ? "&#39;" : NULL;
        if (e) {
            memcpy(out + n, e, strlen(e));
            n += strlen(e);
        } else {
            out[n++] = *s;
        }
    }
    out[n] = '\0';
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
    put(&o, "PicoBoot-WiFi %s (test build)\n", PICOBOOT_WIFI_BUILD);
    // The SSID may hold < & and quotes: escaped, so it can't put markup or script on the page
    char ssid_html[SETTINGS_SSID_MAX * 6 + 1];
    html_escape(ssid_html, sizeof(ssid_html), wifi_ssid());
    put(&o, "SSID: %s   Password: %s\n\n", ssid_html,
        settings_default_password(wifi_settings()) ? SETTINGS_DEFAULT_PASSWORD " (default)" : "(changed)");
    if (wifi_reverted()) {
        put(&o, "WI-FI CHANGE UNDONE at this start: no client joined with it (design D41)\n\n");
    }
    if (store_failed) {
        put(&o, "LAST CHANGE NOT SAVED: the write to flash failed\n\n");
    }

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
    put(&o, "  Wi-Fi start        : CYW43 init %lu ms, AP start %lu ms\n",
        (unsigned long)info.cyw43_init_ms, (unsigned long)info.ap_start_ms);
    {
        // QMI M0 read: command byte, widths (0 = x1, 1 = x2, 2 = x4), dummy bits, clock divider
        const uint32_t rfmt = info.qmi_rfmt, div = info.qmi_timing & 0xFFu;
        const uint32_t mbps100 = info.dma_64k_us ? (uint32_t)(65536ull * 100 / info.dma_64k_us) : 0;
        put(&o, "  Flash read         : cmd 0x%02lx, addr x%u data x%u, %lu dummy bits, clkdiv %lu "
                "(SCK %lu MHz at %lu MHz)\n", (unsigned long)(info.qmi_rcmd & 0xFFu),
            1u << ((rfmt >> 2) & 3u), 1u << ((rfmt >> 8) & 3u), (unsigned long)(((rfmt >> 16) & 7u) * 4u),
            (unsigned long)div, (unsigned long)(div ? info.sys_khz / 1000 / div : 0), (unsigned long)(info.sys_khz / 1000));
        put(&o, "  Payload DMA speed  : %lu.%02lu MB/s through the uncached window (the console needs ~3.3)\n",
            (unsigned long)(mbps100 / 100), (unsigned long)(mbps100 % 100));
    }
    put(&o, "  Uptime             : %02lu:%02lu:%02lu\n", (unsigned long)(up_s / 3600),
        (unsigned long)(up_s / 60 % 60), (unsigned long)(up_s % 60));
    put(&o, "  Temperature        : %.1f C (RP2350 sensor; reads low on a low 3.3 V rail)\n", (double)page_temperature_c());
    put(&o, "  Wi-Fi clients      : %d\n", net_client_count());
    put_settings(&o, "Settings", wifi_settings(), wifi_settings_stored());

    bootctl_t b;
    const bool stored = storage_bootctl_load(&b);
    static const char *const states[] = {"installed", "empty", "TRIAL", "kept", "FAILED"};
    put(&o, "  Slots              : A %s, B %s%s; running %c\n",
        b.slot_state[0] < 5 ? states[b.slot_state[0]] : "?", b.slot_state[1] < 5 ? states[b.slot_state[1]] : "?",
        b.pending_slot == BOOTCTL_SLOT_A ? " (A pending)" : b.pending_slot == BOOTCTL_SLOT_B ? " (B pending)" : "",
        r->slot == BOOTCTL_SLOT_B ? 'B' : 'A');
    put(&o, "  Next power-on      : %s; CS %s%s\n",
        b.active_payload == BOOTCTL_PAYLOAD_BUILTIN ? "built-in gekkoboot" :
        b.active_payload == BOOTCTL_PAYLOAD_2 ? "payload 2 (built-in if not valid)" : "payload 1 (built-in if not valid)",
        (b.flags & BOOTCTL_FLAG_TEST_CS_PULLDOWN) ? "PULL-DOWN (test, as PicoBoot)" : "pull-up (D36)",
        stored ? "" : " (boot control empty: defaults)");

    put_log(&o);

    put(&o, "</pre>"
            "<form method=post action=/payload1><button>Next: payload 1</button></form>"
            "<form method=post action=/payload2><button>Next: payload 2</button></form>"
            "<form method=post action=/builtin><button>Next: built-in gekkoboot</button></form><br>"
            "<form method=post action=/cs-pullup><button>CS pull-up (normal)</button></form>"
            "<form method=post action=/cs-pulldown><button>CS pull-down (test)</button></form><br>"
            "<form method=post action=/reboot-wd><button>Reboot (watchdog)</button></form>"
            "<form method=post action=/reboot-rom><button>Reboot (ROM)</button></form>"
            "<a href=\"/test\">Refresh</a> <a href=\"/\">Web UI</a></body></html>\n");
    return o.len;
}
