/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Renders the page into buf and returns its length (net.c calls it per request)
typedef size_t (*net_page_fn)(char *buf, size_t size);

// Handles a POST to `path` (a button on the page); returns false if it isn't one
typedef bool (*net_action_fn)(const char *path);

// Handles a request under /api/ (docs/api.md): a GET, or a POST with its JSON body. Writes the JSON
// answer into out and its length into *out_len; returns the HTTP status.
typedef int (*net_api_fn)(bool post, const char *path, const char *body, size_t body_len,
                          char *out, size_t out_size, size_t *out_len);

// An upload under /api/ with Content-Type: application/octet-stream (D18), streamed as it arrives.
// begin() gets the request's headers (net_header() reads one) and returns 200 to take it, or an HTTP
// status with the JSON answer in out; data() gets the body's bytes in order; end() writes the JSON
// answer (complete: all of Content-Length arrived) and returns its status. One upload at a time.
typedef struct {
    bool (*handles)(const char *path);
    int (*begin)(const char *path, const char *headers, uint16_t headers_len, uint32_t length,
                 char *out, size_t out_size, size_t *out_len);
    void (*data)(const uint8_t *bytes, size_t length);
    int (*end)(bool complete, char *out, size_t out_size, size_t *out_len);
} net_upload_t;

// The access point's settings (design D30, D40)
typedef struct {
    const char *ssid;
    const char *password;           // WPA2-AES
    unsigned channel;
    uint32_t country;               // CYW43_COUNTRY(...)
} net_ap_t;

// Starts the access point, the DHCP and DNS servers (every name answers with 192.168.4.1, so phones
// open the page by themselves) and the HTTP server. cyw43_arch_init_with_country() must have
// succeeded first. Returns false if the chip doesn't report the access point up.
// `uploads`: the upload handlers, ending with NULL
bool net_start(const net_ap_t *ap, net_page_fn page, net_action_fn action, net_api_fn api,
               const net_upload_t *const *uploads);

// The value of a request header (`name` in lower case with its colon, e.g. "x-name:") from the
// headers a net_upload_t's begin() gets, without spaces around it; false if there's none or it
// doesn't fit out_size
bool net_header(const char *headers, uint16_t headers_len, const char *name, char *out, size_t out_size);

// Restarts the access point with other settings (design D41); every client drops off. The HTTP
// server keeps running. Call from the main loop, not from an lwIP callback. Returns false if the
// chip didn't take the access point down: then it still runs the old settings.
bool net_restart(const net_ap_t *ap);

// One iteration of the main loop: lets the CYW43 driver and lwIP do their work
void net_poll(void);

// Requests the HTTP server has taken, of any kind (D41: a client came through)
uint32_t net_requests(void);

// An upload is streaming in: the API refuses changes that would reboot or restart the AP meanwhile
bool net_upload_running(void);

// A warm reboot half a second from now, after the answer is out
void net_reboot_soon(bool through_rom);

// Connected clients, refreshed once a second by net_poll() (never asked of the CYW43 from inside
// an lwIP callback)
int net_client_count(void);
