/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests: the lwIP UDP and pbuf calls the DHCP server makes (test_dhcpserver.c implements them)
#pragma once
#include "lwip/ip_addr.h"

struct pbuf { struct pbuf *next; void *payload; u16_t tot_len, len; };
struct udp_pcb { int unused; };
typedef void (*udp_recv_fn)(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
enum { PBUF_TRANSPORT, PBUF_RAM };

struct pbuf *pbuf_alloc(int layer, u16_t len, int type);
void pbuf_free(struct pbuf *p);
u16_t pbuf_copy_partial(const struct pbuf *p, void *out, u16_t len, u16_t offset);
struct udp_pcb *udp_new(void);
void udp_recv(struct udp_pcb *pcb, udp_recv_fn fn, void *arg);
void udp_remove(struct udp_pcb *pcb);
err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port);
void udp_bind_netif(struct udp_pcb *pcb, const struct netif *netif);
err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *dest, u16_t port);
err_t udp_sendto_if(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *dest, u16_t port, struct netif *netif);
struct netif *ip_current_input_netif(void);
