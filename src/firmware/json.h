/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Just enough JSON for the API (docs/api.md): a writer into a fixed buffer, and a reader for flat
// objects (string, integer, boolean and null members), the shape of every POST body.
//
// Plain C without SDK calls, for host tests.

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Writer ---------------------------------------------------------------------------------------

#define JSON_DEPTH_MAX 6

typedef struct {
    char *buf;
    size_t size, len;
    bool overflow;
    unsigned depth;
    bool first[JSON_DEPTH_MAX + 1];     // Nothing written yet at this depth: no comma before the next
} json_out_t;

// `key` is the member name inside an object, NULL at the top and inside arrays
void json_init(json_out_t *j, char *buf, size_t size);
void json_object_begin(json_out_t *j, const char *key);
void json_object_end(json_out_t *j);
void json_array_begin(json_out_t *j, const char *key);
void json_array_end(json_out_t *j);
void json_string(json_out_t *j, const char *key, const char *value);
void json_int(json_out_t *j, const char *key, long value);
void json_bool(json_out_t *j, const char *key, bool value);
void json_tenths(json_out_t *j, const char *key, long tenths);    // 384 is written as 38.4

// The length written (without the NUL), or 0 if it didn't fit or the nesting is off
size_t json_finish(json_out_t *j);

// --- Reader ---------------------------------------------------------------------------------------

#define JSON_KEY_MAX    32
#define JSON_STRING_MAX 64

typedef enum { JSON_STRING, JSON_NUMBER, JSON_BOOL, JSON_NULL } json_type_t;

typedef struct {
    json_type_t type;
    char string[JSON_STRING_MAX + 1];   // JSON_STRING, NUL-terminated; a \u escape above 0x7F becomes 0x80
    bool too_long;                      // JSON_STRING: longer than JSON_STRING_MAX, `string` holds the start
    long number;                        // JSON_NUMBER, if `integer`
    bool integer;                       // JSON_NUMBER: a whole number of at most 9 digits
    bool boolean;                       // JSON_BOOL
} json_value_t;

// Called for each member in order; returning false stops the parse (json_parse_object() then
// returns JSON_STOPPED)
typedef bool (*json_member_fn)(void *ctx, const char *key, const json_value_t *value);

typedef enum {
    JSON_OK,
    JSON_SYNTAX,        // Not a flat JSON object: bad syntax, nesting, a key over JSON_KEY_MAX, or trailing text
    JSON_STOPPED,       // The callback said stop
} json_result_t;

json_result_t json_parse_object(const char *text, size_t len, json_member_fn fn, void *ctx);
