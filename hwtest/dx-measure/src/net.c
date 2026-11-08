/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Access point plus a tiny HTTP server on lwIP's raw TCP API (single core, poll mode):
//   GET  /stream?mb=N  N MB of data (Tx load test)
//   POST /sink         reads and discards the body (Rx load test)
//   POST /clear        clears the boot log
//   POST /builtin      next boots use the built-in IPL; POST /flash undoes that
//   POST /late         next boot only: Wi-Fi starts 60 s after the injection
//   POST /reboot-wd    warm reboot through the watchdog; POST /reboot-rom through rom_reboot()
//   GET  anything else the report page (also answers phones' captive-portal probes)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "net.h"
#include "report.h"
#include "bootlog.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"
#include "hardware/watchdog.h"

#define HTTP_PORT       80
#define REQ_MAX         1024        // Request line and headers
#define PAGE_MAX        8192        // Rendered report page
#define STREAM_MB_MAX   500
#define IDLE_POLLS      8           // Polled every 2 coarse timer ticks (1 s): close after ~8 s idle

typedef enum { CONN_REQUEST, CONN_SINK, CONN_SEND } conn_state_t;

typedef struct {
    conn_state_t state;
    char req[REQ_MAX + 1];
    size_t req_len;
    // CONN_SINK
    uint32_t body_expected, body_received;
    uint64_t sink_start_us;
    // CONN_SEND: a header/page buffer, then optionally a repeated pattern
    char *page;
    size_t page_len, page_sent;
    uint32_t stream_left;
    uint idle;
} conn_t;

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static uint8_t pattern[TCP_MSS];
static int client_count;
static uint64_t next_client_check_us;
static int open_conns;                  // While > 0, net_poll() doesn't sleep
static enum { REBOOT_NONE, REBOOT_WATCHDOG, REBOOT_ROM } pending_reboot;
static uint64_t reboot_at_us;

static void conn_free(struct tcp_pcb *pcb, conn_t *c) {
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_poll(pcb, NULL, 0);
    }
    if (c) {
        free(c->page);
        free(c);
        open_conns--;
    }
}

static void conn_close(struct tcp_pcb *pcb, conn_t *c) {
    conn_free(pcb, c);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

// Queues as much of the page and the stream as the send buffer takes; closes when done
static void send_more(struct tcp_pcb *pcb, conn_t *c) {
    while (c->page_sent < c->page_len) {
        u16_t room = tcp_sndbuf(pcb);
        if (room == 0) {
            break;
        }
        size_t n = c->page_len - c->page_sent;
        if (n > room) {
            n = room;
        }
        if (tcp_write(pcb, c->page + c->page_sent, (u16_t)n, TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE) != ERR_OK) {
            break;
        }
        c->page_sent += n;
    }
    while (c->page_sent == c->page_len && c->stream_left > 0) {
        u16_t room = tcp_sndbuf(pcb);
        size_t n = c->stream_left < sizeof(pattern) ? c->stream_left : sizeof(pattern);
        if (room < n || tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN - 1) {
            break;
        }
        // The pattern is static, so lwIP can reference it without copying
        if (tcp_write(pcb, pattern, (u16_t)n, TCP_WRITE_FLAG_MORE) != ERR_OK) {
            break;
        }
        c->stream_left -= n;
    }
    tcp_output(pcb);
    if (c->page_sent == c->page_len && c->stream_left == 0) {
        conn_close(pcb, c);     // lwIP still sends what's queued, then FIN
    }
}

static void respond(struct tcp_pcb *pcb, conn_t *c, const char *status, const char *type,
                    const char *extra_headers, const char *body, size_t body_len, uint32_t stream_bytes) {
    size_t cap = 256 + body_len;
    c->page = malloc(cap);
    if (!c->page) {
        conn_close(pcb, c);
        return;
    }
    int n = snprintf(c->page, cap,
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n%sCache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, type, (unsigned long)(body_len + stream_bytes), extra_headers);
    if (n < 0 || (size_t)n + body_len > cap) {
        conn_close(pcb, c);
        return;
    }
    memcpy(c->page + n, body, body_len);
    c->page_len = (size_t)n + body_len;
    c->page_sent = 0;
    c->stream_left = stream_bytes;
    c->state = CONN_SEND;
    send_more(pcb, c);
}

static void respond_page(struct tcp_pcb *pcb, conn_t *c) {
    char *page = malloc(PAGE_MAX);
    if (!page) {
        conn_close(pcb, c);
        return;
    }
    size_t len = report_render(page, PAGE_MAX);
    respond(pcb, c, "200 OK", "text/html; charset=utf-8", "", page, len, 0);
    free(page);
}

static void respond_sink(struct tcp_pcb *pcb, conn_t *c, bool complete) {
    char body[512];
    size_t len = report_render_sink(body, sizeof(body), c->body_received,
                                    time_us_64() - c->sink_start_us, complete);
    respond(pcb, c, "200 OK", "text/html; charset=utf-8", "", body, len, 0);
}

static uint32_t header_value(const char *headers, const char *name) {
    // Case-insensitive search for "\r\n<name>:" in the request headers
    size_t name_len = strlen(name);
    for (const char *p = strstr(headers, "\r\n"); p; p = strstr(p + 2, "\r\n")) {
        if (strncasecmp(p + 2, name, name_len) == 0 && p[2 + name_len] == ':') {
            return (uint32_t)strtoul(p + 3 + name_len, NULL, 10);
        }
    }
    return 0;
}

// May free c (a response that fits the send buffer is closed right away): don't use c afterwards
static void handle_request(struct tcp_pcb *pcb, conn_t *c, size_t body_start, uint32_t body_overflow) {
    char method[8] = {0}, path[96] = {0};
    sscanf(c->req, "%7s %95s", method, path);

    if (strcmp(method, "GET") == 0 && strncmp(path, "/stream", 7) == 0) {
        const char *q = strstr(path, "mb=");
        uint32_t mb = q ? (uint32_t)strtoul(q + 3, NULL, 10) : 20;
        if (mb == 0 || mb > STREAM_MB_MAX) {
            mb = 20;
        }
        char disposition[96];
        snprintf(disposition, sizeof(disposition),
                 "Content-Disposition: attachment; filename=\"stream-%lumb.bin\"\r\n", (unsigned long)mb);
        respond(pcb, c, "200 OK", "application/octet-stream", disposition, "", 0, mb * 1024 * 1024);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/sink") == 0) {
        c->state = CONN_SINK;
        c->sink_start_us = time_us_64();
        c->body_expected = header_value(c->req, "Content-Length");
        c->body_received = (uint32_t)(c->req_len - body_start) + body_overflow;
        if (c->body_received >= c->body_expected) {
            respond_sink(pcb, c, true);
        }
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/clear") == 0) {
        bootlog_clear();
        respond(pcb, c, "303 See Other", "text/plain", "Location: /\r\n", "", 0, 0);
    } else if (strcmp(method, "POST") == 0 &&
               (strcmp(path, "/builtin") == 0 || strcmp(path, "/flash") == 0 || strcmp(path, "/late") == 0)) {
        uint32_t sw = bootlog_switches();
        if (strcmp(path, "/builtin") == 0) {
            sw |= SWITCH_FORCE_BUILTIN;
        } else if (strcmp(path, "/flash") == 0) {
            sw &= ~SWITCH_FORCE_BUILTIN;
        } else {
            sw |= SWITCH_WIFI_LATE;
        }
        bootlog_set_switches(sw);
        respond(pcb, c, "303 See Other", "text/plain", "Location: /\r\n", "", 0, 0);
    } else if (strcmp(method, "POST") == 0 && (strcmp(path, "/reboot-wd") == 0 || strcmp(path, "/reboot-rom") == 0)) {
        // Reboot shortly after the response is out, from net_poll() (not inside lwIP)
        pending_reboot = strcmp(path, "/reboot-wd") == 0 ? REBOOT_WATCHDOG : REBOOT_ROM;
        reboot_at_us = time_us_64() + 500 * 1000;
        static const char page[] = "<!doctype html><meta http-equiv=refresh content=\"8;url=/\">"
                                   "<pre>Rebooting... this page reloads in 8 s (reconnect to the Wi-Fi if needed)</pre>";
        respond(pcb, c, "200 OK", "text/html; charset=utf-8", "", page, sizeof(page) - 1, 0);
    } else {
        respond_page(pcb, c);
    }
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = arg;
    if (!p) {                                   // The client closed its side
        if (c && c->state == CONN_SINK) {
            respond_sink(pcb, c, false);
        } else {
            conn_close(pcb, c);
        }
        return ERR_OK;
    }
    if (err != ERR_OK || !c) {
        pbuf_free(p);
        return ERR_OK;
    }
    c->idle = 0;
    tcp_recved(pcb, p->tot_len);

    if (c->state == CONN_REQUEST) {
        size_t room = REQ_MAX - c->req_len;
        size_t n = p->tot_len < room ? p->tot_len : room;
        pbuf_copy_partial(p, c->req + c->req_len, (u16_t)n, 0);
        c->req_len += n;
        c->req[c->req_len] = '\0';
        // Bytes beyond REQ_MAX can only be upload body; count them for the sink
        uint32_t overflow = (uint32_t)(p->tot_len - n);
        pbuf_free(p);
        char *end = strstr(c->req, "\r\n\r\n");
        if (end) {
            handle_request(pcb, c, (size_t)(end + 4 - c->req), overflow);
        } else if (c->req_len == REQ_MAX) {
            respond(pcb, c, "431 Request Header Fields Too Large", "text/plain", "", "", 0, 0);
        }
    } else if (c->state == CONN_SINK) {
        c->body_received += p->tot_len;
        pbuf_free(p);
        if (c->body_received >= c->body_expected) {
            respond_sink(pcb, c, true);
        }
    } else {
        pbuf_free(p);                           // Ignore anything the client sends while we send
    }
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;
    conn_t *c = arg;
    if (c && c->state == CONN_SEND) {
        c->idle = 0;
        send_more(pcb, c);
    }
    return ERR_OK;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
    conn_t *c = arg;
    if (c && ++c->idle > IDLE_POLLS) {
        conn_free(pcb, c);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)err;
    conn_free(NULL, arg);                       // lwIP has already freed the pcb
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !pcb) {
        return ERR_VAL;
    }
    conn_t *c = calloc(1, sizeof(conn_t));
    if (!c) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    open_conns++;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
    tcp_poll(pcb, on_poll, 2);
    return ERR_OK;
}

bool net_start(const char *ssid, const char *password) {
    for (size_t i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (uint8_t)('A' + i % 26);
    }

    cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t gw, mask;
    gw.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    mask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
    dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    dns_server_init(&dns_server, &cyw43_state.netif[CYW43_ITF_AP], &gw);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb || tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) {
        return false;
    }
    struct tcp_pcb *listener = tcp_listen_with_backlog(pcb, 4);
    if (!listener) {
        tcp_close(pcb);
        return false;
    }
    tcp_accept(listener, on_accept);
    return true;
}

void net_poll(void) {
    cyw43_arch_poll();
    if (open_conns == 0) {
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
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

    if (time_us_64() >= next_client_check_us) {
        int num = 8;
        uint8_t macs[8 * 6];
        cyw43_wifi_ap_get_stas(&cyw43_state, &num, macs);
        client_count = num;
        next_client_check_us = time_us_64() + 1000 * 1000;
    }
}

int net_client_count(void) {
    return client_count;
}
