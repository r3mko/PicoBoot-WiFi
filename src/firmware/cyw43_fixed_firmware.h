/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// The CYW43 driver's firmware source (design D14): instead of an array compiled into the image,
// the driver reads the blob from Wi-Fi area A or B in flash. Selected with
// CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE (src/firmware/CMakeLists.txt), the same hook the SDK's
// partition variant uses (cyw43_partition_firmware.h). wifiblob_select() fills in the values
// before cyw43_arch_init().

#include <stdint.h>

extern int cyw43_wifi_fw_len;
extern int cyw43_clm_len;
extern uintptr_t fw_data;

#define CYW43_WIFI_FW_LEN (cyw43_wifi_fw_len)
#define CYW43_CLM_LEN (cyw43_clm_len)
