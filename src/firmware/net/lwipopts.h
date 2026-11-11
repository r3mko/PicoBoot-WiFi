/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// lwIP options for the firmware (from the phase 0 measurement build): an access point with lwIP
// httpd, polled from the main loop (pico_cyw43_arch_lwip_poll). Based on pico-examples'
// lwipopts_examples_common.h, without the DNS client.
// See https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html

#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             1       // Fine in poll mode
// lwIP's pools (about 38 KB) aren't zeroed at start-up, which runs on the boot ROM's slow clock before
// the injection (D10): lwIP's own option for pools in uninitialised memory zeroes them in lwip_init(),
// after the injection (phase 4 review)
#define MEMP_MEM_INIT               1
#define LWIP_DECLARE_MEMORY_ALIGNED(variable_name, size) \
    u8_t variable_name[LWIP_MEM_ALIGN_BUFFER(size)] __attribute__((section(".uninitialized_data.lwip")))
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_TCP_PCB            8
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1       // DHCP and DNS servers
// Needed although we are the DHCP server, not a client: it also sets
// IP_ACCEPT_LINK_LAYER_ADDRESSING, without which lwIP drops packets from 0.0.0.0 (ip4.c),
// and every DHCP DISCOVER comes from 0.0.0.0.
#define LWIP_DHCP                   1
#define LWIP_DNS                    0
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_TCP_KEEPALIVE          0
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

// lwIP httpd (design D18): every response is a custom file made in net.c, headers included
#define LWIP_HTTPD_CUSTOM_FILES     1
#define LWIP_HTTPD_SUPPORT_POST     1
#define LWIP_HTTPD_DYNAMIC_HEADERS  0
#define HTTPD_USE_CUSTOM_FSDATA     1       // net/fsdata_custom.c
#define HTTPD_LIMIT_SENDING_TO_2MSS 0       // Fill the send buffer, for throughput
#define LWIP_HTTPD_DYNAMIC_FILE_READ 1      // Downloads: httpd reads them through fs_read_custom()
// Copy response data into the TCP segments: net.c frees a response when httpd closes it, which
// can be before the client has acknowledged (and before a retransmission of) the last segment
#define HTTP_IS_DATA_VOLATILE(hs)   TCP_WRITE_FLAG_COPY

#endif
