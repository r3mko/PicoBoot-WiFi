/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The access point and a minimal HTTP server on lwIP's raw TCP API (single core, poll mode), from
// the phase 0 measurement build with the phase 0 review's fixes (a stalled send is retried from the
// poll callback, a client's FIN doesn't cut a response short, no tcp_abort fallback inside
// callbacks). Phase 2 replaces it with lwIP httpd and the real web UI.
//
//   POST /reboot-wd    warm reboot through the watchdog (test of design D8)
//   POST /reboot-rom   warm reboot through rom_reboot()
//   POST other paths   the page's actions (net_action_fn), then back to the page
//   GET  anything else the page (also answers phones' captive-portal probes)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net.h"
#include "dhcpserver.h"
#include "dnsserver.h"
#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"
#include "hardware/watchdog.h"

#define HTTP_PORT       80
#define REQ_MAX         1024        // Request line and headers
#define PAGE_MAX        10240       // Rendered page (with the boot log table)
#define POLL_INTERVAL   2           // tcp_poll() in coarse timer ticks (0.5 s each)
#define IDLE_POLLS      10          // Close a connection after ~10 s without progress

typedef struct {
    bool sending;
    char req[REQ_MAX + 1];
    size_t req_len;
    char *out;                      // Response (headers and body)
    size_t out_len, out_sent;
    unsigned idle;
} conn_t;

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static net_page_fn render_page;
static net_action_fn page_action;
static int open_conns;              // While > 0, net_poll() doesn't sleep
static int client_count;
static uint64_t next_client_check_us;
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
        free(c->out);
        free(c);
        open_conns--;
    }
}

static err_t close_retry(void *arg, struct tcp_pcb *pcb) {
    (void)arg;
    tcp_close(pcb);                 // Try again; lwIP keeps polling until it succeeds
    return ERR_OK;
}

// Closes gracefully: lwIP still sends what's queued, then FIN. If lwIP is short of memory right
// now, it retries from the poll callback instead of aborting inside a callback.
static void conn_close(struct tcp_pcb *pcb, conn_t *c) {
    conn_free(pcb, c);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_poll(pcb, close_retry, POLL_INTERVAL);
    }
}

// Queues as much of the response as the send buffer takes; closes once all of it is queued.
// May free c: don't use it afterwards.
static void send_more(struct tcp_pcb *pcb, conn_t *c) {
    while (c->out_sent < c->out_len) {
        u16_t room = tcp_sndbuf(pcb);
        size_t n = c->out_len - c->out_sent;
        if (n > room) {
            n = room;
        }
        if (n == 0 || tcp_write(pcb, c->out + c->out_sent, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            break;                  // Retried from on_sent() or on_poll()
        }
        c->out_sent += n;
    }
    tcp_output(pcb);
    if (c->out_sent == c->out_len) {
        conn_close(pcb, c);
    }
}

// May free c: don't use it afterwards
static void respond(struct tcp_pcb *pcb, conn_t *c, const char *status, const char *extra_headers,
                    const char *body, size_t body_len) {
    size_t cap = 256 + body_len;
    c->out = malloc(cap);
    if (!c->out) {
        conn_close(pcb, c);
        return;
    }
    int n = snprintf(c->out, cap,
                     "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\n"
                     "%sCache-Control: no-store\r\nConnection: close\r\n\r\n", status, (unsigned)body_len,
                     extra_headers);
    if (n < 0 || (size_t)n + body_len > cap) {
        conn_close(pcb, c);
        return;
    }
    memcpy(c->out + n, body, body_len);
    c->out_len = (size_t)n + body_len;
    c->sending = true;
    send_more(pcb, c);
}

// May free c: don't use it afterwards
static void handle_request(struct tcp_pcb *pcb, conn_t *c) {
    char method[8] = {0}, path[64] = {0};
    sscanf(c->req, "%7s %63s", method, path);

    if (strcmp(method, "POST") == 0 && (strcmp(path, "/reboot-wd") == 0 || strcmp(path, "/reboot-rom") == 0)) {
        // Reboot shortly after the response is out, from net_poll() (not inside lwIP)
        pending_reboot = strcmp(path, "/reboot-wd") == 0 ? REBOOT_WATCHDOG : REBOOT_ROM;
        reboot_at_us = time_us_64() + 500 * 1000;
        static const char body[] = "<!doctype html><meta http-equiv=refresh content=\"8;url=/\">"
                                   "<pre>Rebooting... this page reloads in 8 s (reconnect to the Wi-Fi if needed)</pre>";
        respond(pcb, c, "200 OK", "", body, sizeof(body) - 1);
        return;
    }

    if (strcmp(method, "POST") == 0 && page_action && page_action(path)) {
        respond(pcb, c, "303 See Other", "Location: /\r\n", "", 0);
        return;
    }

    char *page = malloc(PAGE_MAX);
    if (!page) {
        conn_close(pcb, c);
        return;
    }
    size_t len = render_page(page, PAGE_MAX);
    respond(pcb, c, "200 OK", "", page, len);
    free(page);
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = arg;
    if (!p) {
        // The client closed its side. A response being sent is finished first (review finding 5).
        if (!c || !c->sending) {
            conn_close(pcb, c);
        }
        return ERR_OK;
    }
    if (err != ERR_OK || !c) {
        pbuf_free(p);
        return ERR_OK;
    }
    tcp_recved(pcb, p->tot_len);
    if (c->sending) {
        pbuf_free(p);               // Ignore anything the client sends meanwhile
        return ERR_OK;
    }
    c->idle = 0;
    size_t room = REQ_MAX - c->req_len;
    size_t n = p->tot_len < room ? p->tot_len : room;
    pbuf_copy_partial(p, c->req + c->req_len, (u16_t)n, 0);
    c->req_len += n;
    c->req[c->req_len] = '\0';
    pbuf_free(p);
    if (strstr(c->req, "\r\n\r\n") || c->req_len == REQ_MAX) {
        handle_request(pcb, c);     // Anything after the headers is ignored
    }
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;
    conn_t *c = arg;
    if (c && c->sending) {
        c->idle = 0;
        send_more(pcb, c);
    }
    return ERR_OK;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
    conn_t *c = arg;
    if (!c) {
        return ERR_OK;
    }
    if (++c->idle > IDLE_POLLS) {
        conn_free(pcb, c);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    if (c->sending) {
        send_more(pcb, c);          // Retry a write that failed with nothing in flight (finding 4)
    }
    return ERR_OK;
}

static void on_err(void *arg, err_t err) {
    (void)err;
    conn_free(NULL, arg);           // lwIP has already freed the pcb
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
    tcp_poll(pcb, on_poll, POLL_INTERVAL);
    return ERR_OK;
}

bool net_start(const char *ssid, const char *password, net_page_fn page, net_action_fn action) {
    render_page = page;
    page_action = action;
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
