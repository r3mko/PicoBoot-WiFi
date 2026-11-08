/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Stand-in for the Pico SDK header in the host tests: only what the tested sources use.

#pragma once

#define XIP_BASE 0x10000000u
#define XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE 0x1c000000u
