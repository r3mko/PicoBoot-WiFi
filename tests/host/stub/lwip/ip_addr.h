/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests: the lwIP address types the DHCP server uses, as lwIP has them on a little-endian chip
// (the address in network order in memory)
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef int8_t err_t;
#define ERR_OK 0

typedef struct { u32_t addr; } ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
#define ip_2_ip4(a) (a)
#define ip4_addr_get_u32(a) ((a)->addr)
#define ip_addr_copy(d, s) ((d) = (s))
#define IP4_ADDR(a, b0, b1, b2, b3) ((a)->addr = (u32_t)(b0) | (u32_t)(b1) << 8 | (u32_t)(b2) << 16 | (u32_t)(b3) << 24)
#define IP_ANY_TYPE ((const ip_addr_t *)0)

struct netif { int unused; };
