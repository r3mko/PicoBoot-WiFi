/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests: stands in for cyw43_config.h, for the DHCP server's clock (test_dhcpserver.c sets it)
#pragma once
#include <stdint.h>

uint32_t cyw43_hal_ticks_ms(void);
