/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The access point, the DHCP and DNS servers, and lwIP httpd (design D18), single core in poll
// mode. Every response is a custom file made here, headers included, so this file decides the
// status codes:
//
//   GET  / or /index.html   the page (net_page_fn)
//   GET  /favicon.ico       204: there is none
//   GET  anything else      302 to http://192.168.4.1/: phones probing for a captive portal
//                           (Apple, Android, Windows) get it and open the page by themselves
//   POST /reboot-wd         warm reboot through the watchdog (test of design D8)
//   POST /reboot-rom        warm reboot through rom_reboot()
//   POST other paths        the page's actions (net_action_fn), then 303 back to the page

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

#define PAGE_MAX        10240       // Rendered page (with the boot log table)
#define POSTS_MAX       4           // POSTs being received at once
#define POST_URI_MAX    32

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static net_page_fn render_page;
static net_action_fn page_action;
static int client_count;
static uint64_t next_client_check_us;
static enum { REBOOT_NONE, REBOOT_WATCHDOG, REBOOT_ROM } pending_reboot;
static uint64_t reboot_at_us;

// --- Responses: httpd custom files ------------------------------------------------------------
//
// Each one is a heap buffer with the status line, headers and body, freed when httpd closes the
// file. httpd copies the data into its TCP segments (HTTP_IS_DATA_VOLATILE in lwipopts.h), so the
// buffer may go before the client has acknowledged it.

static int respond(struct fs_file *file, const char *status, const char *extra_headers,
                   const char *body, size_t body_len) {
    static const char fmt[] = "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\n"
                              "%sCache-Control: no-store\r\nConnection: close\r\n\r\n";
    const size_t cap = sizeof(fmt) + 64 + strlen(extra_headers) + body_len;
    char *out = malloc(cap);
    if (!out) {
        return 0;                   // httpd then sends its own error
    }
    const int n = snprintf(out, cap, fmt, status, (unsigned)body_len, extra_headers);
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

static int respond_page(struct fs_file *file) {
    char *page = malloc(PAGE_MAX);
    if (!page) {
        return 0;
    }
    const size_t len = render_page(page, PAGE_MAX);
    const int ok = respond(file, "200 OK", "", page, len);
    free(page);
    return ok;
}

// The names below that start with "/." are only reached as POST results
int fs_open_custom(struct fs_file *file, const char *name) {
    if (strcmp(name, "/index.html") == 0) {
        return respond_page(file);
    }
    if (strcmp(name, "/favicon.ico") == 0) {
        return respond(file, "204 No Content", "", "", 0);
    }
    if (strcmp(name, "/404.html") == 0) {
        // Every unknown path, which includes the phones' captive-portal probes
        return respond(file, "302 Found", "Location: http://192.168.4.1/\r\n", "", 0);
    }
    if (strcmp(name, "/.done") == 0) {
        return respond(file, "303 See Other", "Location: /\r\n", "", 0);
    }
    if (strcmp(name, "/.rebooting") == 0) {
        static const char body[] = "<!doctype html><meta http-equiv=refresh content=\"8;url=/\">"
                                   "<pre>Rebooting... this page reloads in 8 s (reconnect to the Wi-Fi if needed)</pre>";
        return respond(file, "200 OK", "", body, sizeof(body) - 1);
    }
    return 0;
}

void fs_close_custom(struct fs_file *file) {
    free((void *)file->data);
}

// --- POSTs: httpd's callbacks -------------------------------------------------------------------

static struct {
    void *connection;
    char uri[POST_URI_MAX];
} posts[POSTS_MAX];

err_t httpd_post_begin(void *connection, const char *uri, const char *http_request, u16_t http_request_len,
                       int content_len, char *response_uri, u16_t response_uri_len, u8_t *post_auto_wnd) {
    (void)http_request;
    (void)http_request_len;
    (void)post_auto_wnd;
    snprintf(response_uri, response_uri_len, "/.done");    // If refused: back to the page
    if (content_len > 0 || strlen(uri) >= POST_URI_MAX) {
        return ERR_VAL;             // The page's buttons send no body
    }
    for (int i = 0; i < POSTS_MAX; i++) {
        if (!posts[i].connection) {
            posts[i].connection = connection;
            strcpy(posts[i].uri, uri);
            return ERR_OK;
        }
    }
    return ERR_VAL;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    (void)connection;
    pbuf_free(p);                   // No body expected
    return ERR_OK;
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    const char *result = "/.done";
    for (int i = 0; i < POSTS_MAX; i++) {
        if (posts[i].connection == connection) {
            const char *uri = posts[i].uri;
            if (strcmp(uri, "/reboot-wd") == 0 || strcmp(uri, "/reboot-rom") == 0) {
                // Reboot shortly after the response is out, from net_poll() (not inside lwIP)
                pending_reboot = strcmp(uri, "/reboot-wd") == 0 ? REBOOT_WATCHDOG : REBOOT_ROM;
                reboot_at_us = time_us_64() + 500 * 1000;
                result = "/.rebooting";
            } else if (page_action) {
                page_action(uri);   // Unknown paths do nothing: back to the page either way
            }
            posts[i].connection = NULL;
            break;
        }
    }
    snprintf(response_uri, response_uri_len, "%s", result);
}

// --- Start and main loop ------------------------------------------------------------------------

bool net_start(const char *ssid, const char *password, unsigned channel, net_page_fn page, net_action_fn action) {
    render_page = page;
    page_action = action;
    cyw43_wifi_ap_set_channel(&cyw43_state, channel);      // Kept by cyw43_arch_enable_ap_mode()
    cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t gw, mask;
    gw.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
    dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    dns_server_init(&dns_server, &cyw43_state.netif[CYW43_ITF_AP], &gw);

    httpd_init();
    return true;
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
