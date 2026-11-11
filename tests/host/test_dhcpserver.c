/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/net/dhcpserver/dhcpserver.c against stubbed lwIP: a client's offer,
// request and renewals (RFC 2131 4.3.2), and the 10-minute leases. Built and run by tests/test_host.py.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cyw43_config.h"
#include "lwip/udp.h"
#include "net/dhcpserver/dhcpserver.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- lwIP stubs: one server, its replies captured ------------------------------------------------

static uint32_t now_ms;
uint32_t cyw43_hal_ticks_ms(void) { return now_ms; }

static udp_recv_fn receive;
static void *receive_arg;
static struct udp_pcb pcb;
static struct netif netif;

static uint8_t reply[600];
static int reply_len;           // -1: no reply
static uint8_t reply_to[4];     // The destination address

struct pbuf *pbuf_alloc(int layer, u16_t len, int type) {
    (void)layer;
    (void)type;
    struct pbuf *p = calloc(1, sizeof(*p));
    p->payload = malloc(len ? len : 1);
    p->len = p->tot_len = len;
    return p;
}

void pbuf_free(struct pbuf *p) {
    free(p->payload);
    free(p);
}

u16_t pbuf_copy_partial(const struct pbuf *p, void *out, u16_t len, u16_t offset) {
    (void)offset;
    const u16_t n = p->len < len ? p->len : len;
    memcpy(out, p->payload, n);
    return n;
}

struct udp_pcb *udp_new(void) { return &pcb; }
void udp_recv(struct udp_pcb *u, udp_recv_fn fn, void *arg) { (void)u; receive = fn; receive_arg = arg; }
void udp_remove(struct udp_pcb *u) { (void)u; }
err_t udp_bind(struct udp_pcb *u, const ip_addr_t *a, u16_t port) { (void)u; (void)a; (void)port; return ERR_OK; }
void udp_bind_netif(struct udp_pcb *u, const struct netif *n) { (void)u; (void)n; }
struct netif *ip_current_input_netif(void) { return &netif; }

err_t udp_sendto(struct udp_pcb *u, struct pbuf *p, const ip_addr_t *dest, u16_t port) {
    (void)u;
    CHECK(port == 68);
    reply_len = p->len < sizeof(reply) ? p->len : (int)sizeof(reply);
    memcpy(reply, p->payload, (size_t)reply_len);
    memcpy(reply_to, &dest->addr, 4);
    return ERR_OK;
}

err_t udp_sendto_if(struct udp_pcb *u, struct pbuf *p, const ip_addr_t *dest, u16_t port, struct netif *n) {
    (void)n;
    return udp_sendto(u, p, dest, port);
}

// --- Messages ------------------------------------------------------------------------------------

enum { DISCOVER = 1, OFFER = 2, REQUEST = 3, ACK = 5 };

static const uint8_t MAC_A[6] = {0x02, 0, 0, 0, 0, 0xA1};
static const uint8_t MAC_B[6] = {0x02, 0, 0, 0, 0, 0xB2};
static const uint8_t NONE[4] = {0, 0, 0, 0};
static const uint8_t BROADCAST[4] = {255, 255, 255, 255};

// Sends a client message; `requested` goes into option 50 (NULL: none), `ciaddr` into the header.
// Returns the reply's message type, or 0 for none.
static int send(int type, const uint8_t mac[6], const uint8_t *requested, const uint8_t ciaddr[4]) {
    uint8_t m[300];
    memset(m, 0, sizeof(m));
    m[0] = 1;                                   // BOOTREQUEST
    m[1] = 1;
    m[2] = 6;
    memcpy(m + 4, "\x12\x34\x56\x78", 4);       // xid
    memcpy(m + 12, ciaddr, 4);
    memcpy(m + 28, mac, 6);
    memcpy(m + 236, "\x63\x82\x53\x63", 4);     // The magic cookie
    uint8_t *o = m + 240;
    *o++ = 53;
    *o++ = 1;
    *o++ = (uint8_t)type;
    if (requested) {
        *o++ = 50;
        *o++ = 4;
        memcpy(o, requested, 4);
        o += 4;
    }
    *o++ = 255;
    struct pbuf *p = pbuf_alloc(0, sizeof(m), 0);
    memcpy(p->payload, m, sizeof(m));
    reply_len = -1;
    const ip_addr_t from = {0};
    receive(receive_arg, &pcb, p, &from, 68);
    if (reply_len < 243) {
        return 0;
    }
    for (int i = 240; i + 2 < reply_len && reply[i] != 255; i += 2 + reply[i + 1]) {
        if (reply[i] == 53) {
            return reply[i + 2];
        }
    }
    return -1;
}

static const uint8_t *yiaddr(void) { return reply + 16; }

static const uint8_t IP16[4] = {192, 168, 4, 16};
static const uint8_t IP17[4] = {192, 168, 4, 17};

// --- Tests ---------------------------------------------------------------------------------------

static dhcp_server_t server;

static void start(void) {
    ip_addr_t ip, nm;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&nm, 255, 255, 255, 0);
    dhcp_server_init(&server, &netif, &ip, &nm);
    CHECK(receive != NULL);
}

// Joining: an offer, then the request for it, both broadcast
static void test_join(void) {
    start();
    now_ms = 1000;
    CHECK(send(DISCOVER, MAC_A, NULL, NONE) == OFFER);
    CHECK(memcmp(yiaddr(), IP16, 4) == 0);
    CHECK(memcmp(reply_to, BROADCAST, 4) == 0);
    CHECK(send(REQUEST, MAC_A, IP16, NONE) == ACK);
    CHECK(memcmp(yiaddr(), IP16, 4) == 0);
    CHECK(memcmp(reply_to, BROADCAST, 4) == 0);
    // Rejoining (INIT-REBOOT): option 50, no ciaddr
    CHECK(send(REQUEST, MAC_A, IP16, NONE) == ACK);
}

// Renewing (and rebinding): no option 50, the address in ciaddr. The answer goes to that address, and
// the lease runs 10 minutes from the renewal
static void test_renew(void) {
    start();
    now_ms = 1000;
    CHECK(send(REQUEST, MAC_A, IP16, NONE) == ACK);
    now_ms = 1000 + 5 * 60 * 1000;              // T1: half the lease
    CHECK(send(REQUEST, MAC_A, NULL, IP16) == ACK);
    CHECK(memcmp(yiaddr(), IP16, 4) == 0);
    CHECK(memcmp(reply_to, IP16, 4) == 0);
    // At 13 minutes the first lease would have run out; the renewed one still holds
    now_ms = 1000 + 13 * 60 * 1000;
    CHECK(send(REQUEST, MAC_B, IP16, NONE) == 0);
    CHECK(send(DISCOVER, MAC_B, NULL, NONE) == OFFER);
    CHECK(memcmp(yiaddr(), IP17, 4) == 0);
    // Not renewed again: at 17 minutes it has run out, and another client may take it
    now_ms = 1000 + 17 * 60 * 1000;
    CHECK(send(REQUEST, MAC_B, IP16, NONE) == ACK);
}

// What isn't answered: no address at all, one outside the network, one another client holds
static void test_refused(void) {
    start();
    now_ms = 1000;
    CHECK(send(REQUEST, MAC_A, NULL, NONE) == 0);
    static const uint8_t elsewhere[4] = {10, 0, 0, 16};
    CHECK(send(REQUEST, MAC_A, NULL, elsewhere) == 0);
    static const uint8_t past_pool[4] = {192, 168, 4, 24};
    CHECK(send(REQUEST, MAC_A, NULL, past_pool) == 0);
    CHECK(send(REQUEST, MAC_A, IP16, NONE) == ACK);
    CHECK(send(REQUEST, MAC_B, NULL, IP16) == 0);
}

int main(void) {
    test_join();
    test_renew();
    test_refused();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("dhcpserver: all tests passed\n");
    return 0;
}
