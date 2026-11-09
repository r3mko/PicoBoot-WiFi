/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The access point, the DHCP and DNS servers, and lwIP httpd (design D18), single core in poll
// mode. Every response is a custom file made here, headers included, so this file decides the
// status codes:
//
//   GET  / or /index.html   the web UI: lwIP's built-in files (fsdata_custom.c, made from src/web/)
//   GET  /api/...           the JSON API (net_api_fn, docs/api.md)
//   GET  /favicon.ico       204: there is none
//   GET  anything else      302 to http://192.168.4.1/: phones probing for a captive portal
//                           (Apple, Android, Windows) get it and open the page by themselves
//   POST /api/...           the JSON API, with a JSON body
//
// With a test page (test builds, net_page_fn given):
//   GET  /test              the test page
//   POST /reboot-wd         warm reboot through the watchdog (test of design D8)
//   POST /reboot-rom        warm reboot through rom_reboot()
//   POST other paths        the test page's actions (net_action_fn), then 303 back to it

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/priv/tcp_priv.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"
#include "hardware/watchdog.h"

#define PAGE_MAX        10240       // Rendered test page (with the boot log table)
#define API_GET_MAX     32768       // The largest GET answer: diagnostics with 33 reports, at most
                                    // about 18 KB today (phase 2 review), with room for more fields
#define API_POST_MAX    512         // POST answers are short
#define API_BODY_MAX    1024        // docs/api.md
#define POSTS_MAX       4           // POSTs being received at once
#define POST_URI_MAX    32

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static net_page_fn render_page;
static net_action_fn page_action;
static net_api_fn api_handler;
static uint32_t requests;
static int client_count;
static uint64_t next_client_check_us;
static enum { REBOOT_NONE, REBOOT_WATCHDOG, REBOOT_ROM } pending_reboot;
static uint64_t reboot_at_us;

// --- Responses: httpd custom files ------------------------------------------------------------
//
// Each one is a heap buffer with the status line, headers and body, freed when httpd closes the
// file. httpd copies the data into its TCP segments (HTTP_IS_DATA_VOLATILE in lwipopts.h), so the
// buffer may go before the client has acknowledged it.

static const char *status_text(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 204: return "204 No Content";
        case 302: return "302 Found";
        case 303: return "303 See Other";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 413: return "413 Payload Too Large";
        case 415: return "415 Unsupported Media Type";
        case 503: return "503 Service Unavailable";
        default: return "500 Internal Server Error";
    }
}

// When there's no memory for an answer: a fixed one, never freed (phase 2 review finding 7)
static const char OUT_OF_MEMORY[] =
    "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\nContent-Length: 53\r\n"
    "Cache-Control: no-store\r\nConnection: close\r\n\r\n"
    "{\"error\":\"busy\",\"message\":\"out of memory, try again\"}";

static int respond_out_of_memory(struct fs_file *file) {
    memset(file, 0, sizeof(*file));
    file->data = OUT_OF_MEMORY;
    file->len = file->index = (int)sizeof(OUT_OF_MEMORY) - 1;
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return 1;
}

static int respond_as(struct fs_file *file, int status, const char *type, const char *extra_headers,
                      const char *body, size_t body_len) {
    static const char fmt[] = "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                              "%sCache-Control: no-store\r\nConnection: close\r\n\r\n";
    const size_t cap = sizeof(fmt) + 96 + strlen(extra_headers) + body_len;
    char *out = malloc(cap);
    if (!out) {
        return respond_out_of_memory(file);
    }
    const int n = snprintf(out, cap, fmt, status_text(status), type, (unsigned)body_len, extra_headers);
    if (n < 0 || (size_t)n + body_len > cap) {
        free(out);
        return 0;
    }
    memcpy(out + n, body, body_len);
    memset(file, 0, sizeof(*file));
    file->data = out;
    file->len = n + (int)body_len;
    file->index = file->len;        // All of it is in memory
    file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
    return 1;
}

static int respond(struct fs_file *file, int status, const char *extra_headers, const char *body, size_t body_len) {
    return respond_as(file, status, "text/html; charset=utf-8", extra_headers, body, body_len);
}

static int respond_json(struct fs_file *file, int status, const char *json, size_t len) {
    return respond_as(file, status, "application/json", "", json, len);
}

static int respond_page(struct fs_file *file) {
    char *page = malloc(PAGE_MAX);
    if (!page) {
        return respond_out_of_memory(file);
    }
    const size_t len = render_page(page, PAGE_MAX);
    const int ok = respond(file, 200, "", page, len);
    free(page);
    return ok;
}

static const char BUSY_JSON[] = "{\"error\":\"busy\",\"message\":\"too many requests at once\"}";

static int respond_api_get(struct fs_file *file, const char *path) {
    char *out = malloc(API_GET_MAX);
    if (!out) {
        return respond_json(file, 503, BUSY_JSON, sizeof(BUSY_JSON) - 1);
    }
    size_t len = 0;
    const int status = api_handler(false, path, NULL, 0, out, API_GET_MAX, &len);
    const int ok = respond_json(file, status, out, len);
    free(out);
    return ok;
}

// The answer to the POST that just ended. httpd opens its response file ("/.api") within the same
// call as httpd_post_finished() or a refusing httpd_post_begin(), so one answer at a time is enough.
static struct {
    int status;
    char json[API_POST_MAX];
    size_t len;
} api_answer;

static void api_error(int status, const char *code, const char *message) {
    api_answer.status = status;
    const int n = snprintf(api_answer.json, sizeof(api_answer.json), "{\"error\":\"%s\",\"message\":\"%s\"}",
                           code, message);
    api_answer.len = n > 0 && (size_t)n < sizeof(api_answer.json) ? (size_t)n : 0;
}

// The names below that start with "/." are only reached as POST results
int fs_open_custom(struct fs_file *file, const char *name) {
    requests++;
    if (strncmp(name, "/api/", 5) == 0 && api_handler) {
        return respond_api_get(file, name);
    }
    if (strcmp(name, "/test") == 0 && render_page) {
        return respond_page(file);
    }
    if (strcmp(name, "/favicon.ico") == 0) {
        return respond(file, 204, "", "", 0);
    }
    if (strcmp(name, "/404.html") == 0) {
        // Every unknown path, which includes the phones' captive-portal probes
        return respond(file, 302, "Location: http://192.168.4.1/\r\n", "", 0);
    }
    if (strcmp(name, "/.api") == 0) {
        return respond_json(file, api_answer.status, api_answer.json, api_answer.len);
    }
    if (strcmp(name, "/.forbidden") == 0) {
        static const char body[] = "<!doctype html><pre>Open PicoBoot at http://192.168.4.1/</pre>";
        return respond(file, 403, "", body, sizeof(body) - 1);
    }
    if (strcmp(name, "/.done") == 0) {
        return respond(file, 303, render_page ? "Location: /test\r\n" : "Location: /\r\n", "", 0);
    }
    if (strcmp(name, "/.rebooting") == 0) {
        static const char body[] = "<!doctype html><meta http-equiv=refresh content=\"8;url=/test\">"
                                   "<pre>Rebooting... this page reloads in 8 s (reconnect to the Wi-Fi if needed)</pre>";
        return respond(file, 200, "", body, sizeof(body) - 1);
    }
    return 0;
}

void fs_close_custom(struct fs_file *file) {
    if (file->data != OUT_OF_MEMORY) {
        free((void *)file->data);
    }
}

// --- POSTs: httpd's callbacks -------------------------------------------------------------------

static struct {
    void *connection;
    char uri[POST_URI_MAX];
    bool api;
    int expected;                   // Content-Length
    size_t len;
    char body[API_BODY_MAX];
} posts[POSTS_MAX];

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// Whether `text` starts with `prefix`, ignoring case
static bool starts_with(const char *text, const char *end, const char *prefix) {
    for (; *prefix; prefix++, text++) {
        if (text >= end || lower(*text) != *prefix) {
            return false;
        }
    }
    return true;
}

// The value of the header `name` (lower case, with the colon), or NULL. httpd ends the headers
// with a NUL; `end` bounds the search as well.
static const char *header_value(const char *headers, u16_t len, const char *name, const char **end_out) {
    const char *end = headers + len;
    for (const char *line = headers; line < end && *line; ) {
        if (starts_with(line, end, name)) {
            const char *v = line + strlen(name);
            while (v < end && *v == ' ') {
                v++;
            }
            *end_out = end;
            return v;
        }
        const char *nl = memchr(line, '\n', (size_t)(end - line));
        if (!nl) {
            break;
        }
        line = nl + 1;
    }
    return NULL;
}

// Whether a header value at v is exactly `word`, or `word` followed by one of `stops`
static bool value_is(const char *v, const char *end, const char *word, const char *stops) {
    if (!starts_with(v, end, word)) {
        return false;
    }
    const char *after = v + strlen(word);
    return after >= end || *after == '\0' || *after == '\r' || strchr(stops, *after);
}

// Content-Type: application/json (docs/api.md: a page on another site can't send it without a
// CORS preflight)
static bool json_content_type(const char *headers, u16_t len) {
    const char *end, *v = header_value(headers, len, "content-type:", &end);
    return v && value_is(v, end, "application/json", "; ");
}

// Host: 192.168.4.1 (docs/api.md). A page elsewhere that points its own name at the Pico (DNS
// rebinding; the Pico's DNS answers every name with this address) would be same-origin otherwise,
// and get past the Content-Type guard (phase 2 review finding 1).
static bool host_is_pico(const char *headers, u16_t len) {
    const char *end, *v = header_value(headers, len, "host:", &end);
    return v && (value_is(v, end, "192.168.4.1", " ") || value_is(v, end, "192.168.4.1:80", " "));
}

err_t httpd_post_begin(void *connection, const char *uri, const char *http_request, u16_t http_request_len,
                       int content_len, char *response_uri, u16_t response_uri_len, u8_t *post_auto_wnd) {
    (void)post_auto_wnd;
    requests++;
    const bool api = strncmp(uri, "/api/", 5) == 0 && api_handler;
    snprintf(response_uri, response_uri_len, api ? "/.api" : "/.done");    // If refused
    if (!host_is_pico(http_request, http_request_len)) {
        if (api) {
            api_error(403, "forbidden", "send requests to http://192.168.4.1/");
        } else {
            snprintf(response_uri, response_uri_len, "/.forbidden");
        }
        return ERR_VAL;
    }
    if (api) {
        if (!json_content_type(http_request, http_request_len)) {
            api_error(415, "unsupported_type", "send the body as Content-Type: application/json");
            return ERR_VAL;
        }
        if (content_len > API_BODY_MAX) {
            api_error(413, "too_large", "the body is over 1024 bytes");
            return ERR_VAL;
        }
        if (strlen(uri) >= POST_URI_MAX) {
            api_error(404, "not_found", "no such endpoint");
            return ERR_VAL;
        }
    } else if (!page_action || content_len > 0 || strlen(uri) >= POST_URI_MAX) {
        return ERR_VAL;             // No test page, or not one of its buttons (they send no body)
    }
    for (int i = 0; i < POSTS_MAX; i++) {
        if (!posts[i].connection) {
            posts[i].connection = connection;
            strcpy(posts[i].uri, uri);
            posts[i].api = api;
            posts[i].expected = content_len;
            posts[i].len = 0;
            return ERR_OK;
        }
    }
    api_error(503, "busy", "too many requests at once");
    return ERR_VAL;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    for (int i = 0; i < POSTS_MAX; i++) {
        if (posts[i].connection == connection) {
            const size_t room = sizeof(posts[i].body) - posts[i].len;
            const size_t n = p->tot_len < room ? p->tot_len : room;
            pbuf_copy_partial(p, posts[i].body + posts[i].len, (u16_t)n, 0);
            posts[i].len += n;
            break;
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

// Also called when a connection closes before its body has all come: then nothing is done
void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    const char *result = "/.done";
    for (int i = 0; i < POSTS_MAX; i++) {
        if (posts[i].connection != connection) {
            continue;
        }
        const char *uri = posts[i].uri;
        const bool complete = posts[i].len == (size_t)posts[i].expected;
        if (posts[i].api) {
            result = "/.api";
            if (!complete) {
                api_error(400, "bad_json", "the body didn't all arrive");
            } else {
                api_answer.status = api_handler(true, uri, posts[i].body, posts[i].len, api_answer.json,
                                                sizeof(api_answer.json), &api_answer.len);
            }
        } else if (!complete) {
            // Nothing: the client went
        } else if (strcmp(uri, "/reboot-wd") == 0 || strcmp(uri, "/reboot-rom") == 0) {
            net_reboot_soon(strcmp(uri, "/reboot-rom") == 0);
            result = "/.rebooting";
        } else if (page_action) {
            page_action(uri);       // Unknown paths do nothing: back to the page either way
        }
        posts[i].connection = NULL;
        break;
    }
    snprintf(response_uri, response_uri_len, "%s", result);
}

// --- Start, restart and main loop ---------------------------------------------------------------

static void ap_up(const net_ap_t *ap) {
    // As cyw43_arch_enable_ap_mode(), with the country of the settings instead of the one given to
    // cyw43_arch_init_with_country(): the driver sends it to the chip each time the AP comes up
    cyw43_wifi_ap_set_channel(&cyw43_state, ap->channel);
    cyw43_wifi_ap_set_ssid(&cyw43_state, strlen(ap->ssid), (const uint8_t *)ap->ssid);
    cyw43_wifi_ap_set_password(&cyw43_state, strlen(ap->password), (const uint8_t *)ap->password);
    cyw43_wifi_ap_set_auth(&cyw43_state, CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_AP, true, ap->country);

    ip4_addr_t gw, mask;
    gw.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
    dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    dns_server_init(&dns_server, &cyw43_state.netif[CYW43_ITF_AP], &gw);
}

bool net_start(const net_ap_t *ap, net_page_fn page, net_action_fn action, net_api_fn api) {
    render_page = page;
    page_action = action;
    api_handler = api;
    ap_up(ap);
    httpd_init();
    return true;
}

// Whether the chip still has the AP's BSS up ("bss" iovar for the AP's bsscfg; an error means down,
// as the driver notes in cyw43_ll_wifi_ap_init())
static bool ap_bss_up(void) {
    uint8_t buf[4 + 4] = "bss";             // The name with its NUL, then the AP's bsscfg index (1)
    const uint32_t ap_bsscfg = 1;
    memcpy(buf + 4, &ap_bsscfg, sizeof(ap_bsscfg));
    if (cyw43_ioctl(&cyw43_state, CYW43_IOCTL_GET_VAR, sizeof(buf), buf, CYW43_ITF_STA) != 0) {
        return false;
    }
    uint32_t up;
    memcpy(&up, buf, sizeof(up));
    return up != 0;
}

bool net_restart(const net_ap_t *ap) {
    // The DHCP server is bound to the AP's network interface, which comes back with another number
    dhcp_server_deinit(&dhcp_server);
    dns_server_deinit(&dns_server);
    // While the BSS is up, the driver keeps the old SSID, channel and password when the AP comes
    // up again (cyw43_ll_wifi_ap_init() returns early), so make sure it went down
    bool down = false;
    for (int i = 0; i < 3 && !down; i++) {
        cyw43_arch_disable_ap_mode();
        down = !ap_bss_up();
    }
    ap_up(ap);
    client_count = 0;
    return down;
}

uint32_t net_requests(void) {
    return requests;
}

void net_reboot_soon(bool through_rom) {
    // From net_poll(), once the answer is out (not inside lwIP)
    pending_reboot = through_rom ? REBOOT_ROM : REBOOT_WATCHDOG;
    reboot_at_us = time_us_64() + 500 * 1000;
}

void net_poll(void) {
    cyw43_arch_poll();
    if (tcp_active_pcbs == NULL) {      // Don't sleep while a connection is open: throughput
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
    }

    if (time_us_64() >= next_client_check_us) {
        int num = 8;
        uint8_t macs[8 * 6];
        cyw43_wifi_ap_get_stas(&cyw43_state, &num, macs);
        client_count = num;
        next_client_check_us = time_us_64() + 1000 * 1000;
    }

    if (pending_reboot != REBOOT_NONE && time_us_64() >= reboot_at_us) {
        if (pending_reboot == REBOOT_WATCHDOG) {
            watchdog_reboot(0, 0, 0);
        } else {
            rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL, 10, 0, 0);
        }
        while (true) {
            tight_loop_contents();
        }
    }
}

int net_client_count(void) {
    return client_count;
}
