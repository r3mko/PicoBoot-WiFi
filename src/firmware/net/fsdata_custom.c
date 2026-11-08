/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// lwIP httpd's built-in files (fs.c includes this file, HTTPD_USE_CUSTOM_FSDATA): none yet. Every
// response is a custom file made in net.c; plan step 2.4 adds the web UI here.

#include "lwip/apps/fs.h"

#define FS_ROOT ((const struct fsdata_file *)NULL)
#define FS_NUMFILES 0
