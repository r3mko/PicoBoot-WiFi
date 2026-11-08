/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "report.h"
#include "measure.h"
#include "net.h"
#include "hardware/adc.h"
#include "hardware/structs/powman.h"
#include "pico/time.h"

#define SKIPPED_TRANSFERS 224    // CS pulses before the first payload transfer (picoboot-dx.c)
#define WORDS_PER_TRANSFER 256   // 1K per transfer
#define LOG_ROWS 32

// picotool puts an RP2350-E10 workaround block in every UF2: 256 bytes of 0xEF, absolute
// family, at 0x10FFFF00 (the end of a 16 MB flash). Without a partition table the boot ROM
// accepts it; on the Pico 2 W's 4 MB flash it would then land in the last page, 0x3FFF00.
#define LAST_PAGE_OFFSET 0x3FFF00u

static report_info_t info;
static boot_record_t log_rows[LOG_ROWS];

typedef struct {
    char *buf;
    size_t size, len;
} out_t;

static void put(out_t *o, const char *fmt, ...) {
    if (o->len >= o->size) {
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

void report_init(const report_info_t *i) {
    info = *i;
    adc_init();
    adc_set_temp_sensor_enabled(true);
}

static float temperature_c(void) {
    adc_select_input(NUM_ADC_CHANNELS - 1);    // The temperature sensor is the last ADC input
    float volts = adc_read() * 3.3f / 4096.0f;
    return 27.0f - (volts - 0.706f) / 0.001721f;
}

static const char *reset_reason(uint32_t chip_reset) {
    if (chip_reset & POWMAN_CHIP_RESET_HAD_POR_BITS) return "POR";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_BOR_BITS) return "BOR";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) return "RUN";
    if (chip_reset & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS |
                      POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
                      POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS |
                      POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS)) return "WDOG";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS) return "GLITCH";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_RESCUE_BITS) return "RESCUE";
    if (chip_reset & (POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS | POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS)) return "DEBUG";
    if (chip_reset & POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS) return "SWCORE";
    return "?";
}

// What the last page of flash holds: shows whether the E10 block of a UF2 install landed there
static const char *last_page_state(void) {
    const uint8_t *p = (const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + LAST_PAGE_OFFSET);
    bool all_ef = true, all_ff = true;
    for (uint i = 0; i < 256; i++) {
        all_ef &= p[i] == 0xEF;
        all_ff &= p[i] == 0xFF;
    }
    return all_ef ? "256 x 0xEF: the UF2's E10 block landed here" : all_ff ? "erased (0xFF)" : "other data";
}

static uint32_t transfers(const boot_record_t *b) {
    return b->payload_words / WORDS_PER_TRANSFER;
}

// One-word verdict for a boot, used in the log table
static const char *verdict(const boot_record_t *b, char *tmp, size_t size) {
    if (b->flags & BOOT_FLAG_SKIPPED) {
        return "skipped: no power reset";
    }
    if (!(b->flags & BOOT_FLAG_FIRST_CS)) {
        return "no CS (bench?)";
    }
    if (b->flags & BOOT_FLAG_STALLED) {
        return "STALLED";
    }
    uint32_t expected = SKIPPED_TRANSFERS + transfers(b);
    if (b->cs_at_end < expected) {
        snprintf(tmp, size, "MISSED %lu CS", (unsigned long)(expected - b->cs_at_end));
        return tmp;
    }
    if (b->cs_at_end > expected) {
        snprintf(tmp, size, "ok (+%lu CS)", (unsigned long)(b->cs_at_end - expected));
        return tmp;
    }
    return "ok";
}

static void put_boot(out_t *o, const boot_record_t *b) {
    char tmp[32];
    uint32_t t = transfers(b);
    put(o, "  Reset reason         : %s (CHIP_RESET 0x%08lx, watchdog reason 0x%lx)\n",
        reset_reason(b->chip_reset), (unsigned long)b->chip_reset, (unsigned long)b->watchdog_reason);
    put(o, "  Payload              : %s, %lu transfers (%lu bytes)\n",
        (b->flags & BOOT_FLAG_FLASH_PAYLOAD) ? "flash payload" : "built-in gekkoboot",
        (unsigned long)t, (unsigned long)(t * 1024));
    put(o, "  Timer at main()      : %lu us\n", (unsigned long)b->timer_at_main_us);
    put(o, "  main() to PIO ready  : %lu us\n", (unsigned long)b->main_to_ready_us);
    if (b->flags & BOOT_FLAG_SKIPPED) {
        put(o, "  Injection            : skipped, not a power reset (POR/BOR/RUN)\n");
    } else if (b->flags & BOOT_FLAG_FIRST_CS) {
        put(o, "  MARGIN               : %ld us from PIO ready to the first CS\n", (long)b->margin_us);
        put(o, "  Injection            : %s, %lu of %lu words, %lu us after the first CS\n",
            (b->flags & BOOT_FLAG_COMPLETE) ? "complete" : "STALLED",
            (unsigned long)b->words_delivered, (unsigned long)b->payload_words, (unsigned long)b->injection_us);
        put(o, "  CS at the end        : %lu, expected %lu (224 + %lu): %s\n",
            (unsigned long)b->cs_at_end, (unsigned long)(SKIPPED_TRANSFERS + t), (unsigned long)t,
            verdict(b, tmp, sizeof(tmp)));
    } else {
        put(o, "  First CS             : not seen within the timeout (no console?)\n");
    }
}

size_t report_render(char *buf, size_t size) {
    out_t o = {buf, size, 0};
    char tmp[32];
    uint64_t now = time_us_64();
    uint32_t up_s = (uint32_t)(now / 1000000);

    put(&o, "<!doctype html><html><head><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>PicoBoot-WiFi phase 0</title>"
            "<style>body{font-family:monospace;margin:12px}pre{white-space:pre-wrap}"
            "form,a{display:inline-block;margin:6px 12px 6px 0}</style></head><body><pre>");
    put(&o, "PicoBoot-WiFi phase 0 measurement build\n");
    put(&o, "Base: PicoBoot-DX v0.10   Build: %s\n", PHASE0_BUILD_ID);
    put(&o, "SSID: %s   Password: %s\n\n", info.ssid, info.password);

    put(&o, "THIS BOOT (#%lu)\n", (unsigned long)info.boot.seq);
    put_boot(&o, &info.boot);
    put(&o, "  CS now               : %lu\n", (unsigned long)cs_counter_read());
    put(&o, "  Wi-Fi up             : %lu ms after the injection ended\n",
        (unsigned long)((info.wifi_up_us - info.injection_end_us) / 1000));
    put(&o, "  Uptime               : %02lu:%02lu:%02lu\n",
        (unsigned long)(up_s / 3600), (unsigned long)(up_s / 60 % 60), (unsigned long)(up_s % 60));
    put(&o, "  Temperature (RP2350) : %.1f C\n", (double)temperature_c());
    put(&o, "  Wi-Fi clients        : %d\n", net_client_count());
    put(&o, "  Flash 0x3FFF00       : %s\n", last_page_state());
    uint32_t sw = bootlog_switches();
    put(&o, "  Next power-on boots  : %s%s\n\n",
        (sw & SWITCH_FORCE_BUILTIN) ? "the built-in gekkoboot (forced)" : "the flash payload if valid, else built-in",
        (sw & SWITCH_WIFI_LATE) ? "; Wi-Fi starts 60 s late (once)" : "");

    uint n = bootlog_read(log_rows, LOG_ROWS);
    put(&o, "BOOT LOG (newest first, %u kept)\n", n);
    put(&o, "  boot  reset  payload   xfers  margin_us  main>ready_us  inject_ms  CS_end/expected  result\n");
    for (uint i = 0; i < n; i++) {
        const boot_record_t *b = &log_rows[i];
        bool cs = b->flags & BOOT_FLAG_FIRST_CS;
        put(&o, "  %4lu  %-5s  %-8s  %5lu  %9ld  %13lu  %9lu  %6lu/%-6lu    %s\n",
            (unsigned long)b->seq, reset_reason(b->chip_reset),
            (b->flags & BOOT_FLAG_FLASH_PAYLOAD) ? "flash" : "built-in",
            (unsigned long)transfers(b), cs ? (long)b->margin_us : -1L,
            (unsigned long)b->main_to_ready_us, cs ? (unsigned long)(b->injection_us / 1000) : 0UL,
            (unsigned long)b->cs_at_end, (unsigned long)(SKIPPED_TRANSFERS + transfers(b)),
            verdict(b, tmp, sizeof(tmp)));
    }
    put(&o, "</pre>"
            "<a href=\"/stream?mb=20\">Download 20 MB (Tx load)</a>"
            "<a href=\"/stream?mb=100\">Download 100 MB</a>"
            "<form method=post action=/sink enctype=multipart/form-data>"
            "<input type=file name=f> <button>Upload (Rx load)</button></form>"
            "<form method=post action=/builtin><button>Next boots: built-in gekkoboot</button></form>"
            "<form method=post action=/flash><button>Next boots: flash payload</button></form>"
            "<form method=post action=/late><button>Next boot: Wi-Fi 60 s late</button></form>"
            "<form method=post action=/clear><button>Clear boot log</button></form>"
            "<a href=\"/\">Refresh</a>"
            "</body></html>\n");
    return o.len;
}

size_t report_render_sink(char *buf, size_t size, uint32_t bytes, uint64_t elapsed_us, bool complete) {
    out_t o = {buf, size, 0};
    double mbit = elapsed_us ? (bytes * 8.0) / (double)elapsed_us : 0.0;
    put(&o, "<!doctype html><html><head><meta charset=utf-8>"
            "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<title>Upload test</title></head><body><pre>"
            "Upload %s: %lu bytes in %lu ms = %.2f Mbit/s\n</pre><a href=\"/\">Back</a></body></html>\n",
        complete ? "complete" : "INCOMPLETE", (unsigned long)bytes,
        (unsigned long)(elapsed_us / 1000), mbit);
    return o.len;
}
