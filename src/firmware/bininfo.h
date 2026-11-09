/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The program version of the firmware image in a slot (design D13: the firmware page shows both
// slots), read from the SDK's binary info as picotool does. Plain C, for host tests.

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// `image` is a slot's contents (`size` bytes), built to run at 0x10000000. Copies the program version
// string into out and returns true, or returns false if there's none to be found.
bool bininfo_version(const uint8_t *image, size_t size, char *out, size_t out_size);
