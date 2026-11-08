/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// Host tests for src/firmware/json.c (the API's JSON, docs/api.md). Built and run by tests/test_host.py.

#include <stdio.h>
#include <string.h>
#include "json.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- Writer ---------------------------------------------------------------------------------------

static void test_writer(void) {
    char buf[256];
    json_out_t j;
    json_init(&j, buf, sizeof(buf));
    json_object_begin(&j, NULL);
    json_int(&j, "api", 1);
    json_string(&j, "s", "a\"b\\c\n\x01");
    json_bool(&j, "t", true);
    json_bool(&j, "f", false);
    json_tenths(&j, "temp", 384);
    json_tenths(&j, "neg", -5);
    json_tenths(&j, "zero", 0);
    json_int(&j, "min", -42);
    json_array_begin(&j, "list");
    json_int(&j, NULL, 1);
    json_string(&j, NULL, "x");
    json_object_begin(&j, NULL);
    json_object_end(&j);
    json_array_end(&j);
    json_object_begin(&j, "empty");
    json_object_end(&j);
    json_object_end(&j);
    const size_t n = json_finish(&j);
    const char *want = "{\"api\":1,\"s\":\"a\\\"b\\\\c\\u000a\\u0001\",\"t\":true,\"f\":false,\"temp\":38.4,"
                       "\"neg\":-0.5,\"zero\":0.0,\"min\":-42,\"list\":[1,\"x\",{}],\"empty\":{}}";
    CHECK(n == strlen(want));
    CHECK(strcmp(buf, want) == 0);
}

static void test_writer_newline_escape(void) {
    char buf[32];
    json_out_t j;
    json_init(&j, buf, sizeof(buf));
    json_string(&j, NULL, "\n");
    CHECK(json_finish(&j) == 8 && strcmp(buf, "\"\\u000a\"") == 0);
}

static void test_writer_overflow_and_nesting(void) {
    char buf[8];
    json_out_t j;
    json_init(&j, buf, sizeof(buf));
    json_object_begin(&j, NULL);
    json_string(&j, "long", "does not fit");
    json_object_end(&j);
    CHECK(json_finish(&j) == 0);                    // Too long: nothing usable
    CHECK(strlen(buf) < sizeof(buf));               // ...and the buffer stays terminated

    char big[64];
    json_init(&j, big, sizeof(big));
    json_object_begin(&j, NULL);
    CHECK(json_finish(&j) == 0);                    // Not closed
    json_object_end(&j);
    CHECK(json_finish(&j) == 2);
    json_object_end(&j);
    CHECK(json_finish(&j) == 0);                    // Closed once too often

    json_init(&j, big, sizeof(big));
    for (int i = 0; i <= JSON_DEPTH_MAX; i++) {
        json_array_begin(&j, NULL);
    }
    CHECK(json_finish(&j) == 0);                    // Deeper than JSON_DEPTH_MAX
}

// --- Reader ---------------------------------------------------------------------------------------

typedef struct {
    int count;
    char keys[8][JSON_KEY_MAX + 1];
    json_value_t values[8];
    int stop_at;                    // Stop at this member (1-based), 0 = never
} seen_t;

static bool collect(void *ctx, const char *key, const json_value_t *v) {
    seen_t *s = ctx;
    if (s->count < 8) {
        strcpy(s->keys[s->count], key);
        s->values[s->count] = *v;
    }
    s->count++;
    return s->stop_at == 0 || s->count < s->stop_at;
}

static json_result_t parse(const char *text, seen_t *s) {
    memset(s, 0, sizeof(*s));
    return json_parse_object(text, strlen(text), collect, s);
}

static void test_reader_members(void) {
    seen_t s;
    CHECK(parse("{}", &s) == JSON_OK && s.count == 0);
    CHECK(parse("  { }  ", &s) == JSON_OK && s.count == 0);
    CHECK(parse("{\"password\": \"new one\", \"channel\": 11, \"led\": false, \"x\": null, \"t\": true, \"n\": -3}", &s) == JSON_OK);
    CHECK(s.count == 6);
    CHECK(strcmp(s.keys[0], "password") == 0 && s.values[0].type == JSON_STRING && strcmp(s.values[0].string, "new one") == 0);
    CHECK(strcmp(s.keys[1], "channel") == 0 && s.values[1].type == JSON_NUMBER && s.values[1].integer && s.values[1].number == 11);
    CHECK(s.values[2].type == JSON_BOOL && !s.values[2].boolean);
    CHECK(s.values[3].type == JSON_NULL);
    CHECK(s.values[4].type == JSON_BOOL && s.values[4].boolean);
    CHECK(s.values[5].type == JSON_NUMBER && s.values[5].integer && s.values[5].number == -3);
    CHECK(parse("\r\n{\t\"a\"\n:\r1\n}\n", &s) == JSON_OK && s.count == 1);
}

static void test_reader_strings(void) {
    seen_t s;
    CHECK(parse("{\"s\": \"q\\\"b\\\\s\\/n\\nt\\tA\\u0041\"}", &s) == JSON_OK);
    CHECK(strcmp(s.values[0].string, "q\"b\\s/n\nt\tAA") == 0);
    CHECK(parse("{\"s\": \"\\u00e9\"}", &s) == JSON_OK && (unsigned char)s.values[0].string[0] == 0x80);   // Non-ASCII
    CHECK(parse("{\"s\": \"ab\\u0000cd\"}", &s) == JSON_OK && strlen(s.values[0].string) == 5);         // NUL can't cut it short
    CHECK(parse("{\"s\": \"caf\xc3\xa9\"}", &s) == JSON_OK && strlen(s.values[0].string) == 5);         // Raw UTF-8 kept, rejected later

    char text[128 + JSON_STRING_MAX];
    char exact[JSON_STRING_MAX + 1];
    memset(exact, 'x', JSON_STRING_MAX);
    exact[JSON_STRING_MAX] = '\0';
    snprintf(text, sizeof(text), "{\"s\": \"%s\"}", exact);
    CHECK(parse(text, &s) == JSON_OK && !s.values[0].too_long && strlen(s.values[0].string) == JSON_STRING_MAX);
    snprintf(text, sizeof(text), "{\"s\": \"%sy\", \"next\": 1}", exact);
    CHECK(parse(text, &s) == JSON_OK && s.values[0].too_long && s.count == 2);   // Flagged, and parsing goes on

    CHECK(parse("{\"s\": \"tab\there\"}", &s) == JSON_SYNTAX);    // A raw control character
    CHECK(parse("{\"s\": \"\\x\"}", &s) == JSON_SYNTAX);          // Unknown escape
    CHECK(parse("{\"s\": \"\\u12\"}", &s) == JSON_SYNTAX);        // Short \u escape
    CHECK(parse("{\"s\": \"open}", &s) == JSON_SYNTAX);
}

static void test_reader_numbers(void) {
    seen_t s;
    CHECK(parse("{\"n\": 0}", &s) == JSON_OK && s.values[0].integer && s.values[0].number == 0);
    CHECK(parse("{\"n\": 999999999}", &s) == JSON_OK && s.values[0].integer && s.values[0].number == 999999999);
    CHECK(parse("{\"n\": 1234567890}", &s) == JSON_OK && !s.values[0].integer);   // 10 digits
    CHECK(parse("{\"n\": 1.5}", &s) == JSON_OK && !s.values[0].integer);
    CHECK(parse("{\"n\": 6e0}", &s) == JSON_OK && !s.values[0].integer);
    CHECK(parse("{\"n\": -0}", &s) == JSON_OK && s.values[0].integer && s.values[0].number == 0);
    CHECK(parse("{\"n\": 01}", &s) == JSON_SYNTAX);               // Leading zero
    CHECK(parse("{\"n\": 1.}", &s) == JSON_SYNTAX);
    CHECK(parse("{\"n\": -}", &s) == JSON_SYNTAX);
    CHECK(parse("{\"n\": +1}", &s) == JSON_SYNTAX);
    CHECK(parse("{\"n\": 1e}", &s) == JSON_SYNTAX);
}

static void test_reader_rejects(void) {
    seen_t s;
    CHECK(parse("", &s) == JSON_SYNTAX);
    CHECK(parse("[]", &s) == JSON_SYNTAX);
    CHECK(parse("\"text\"", &s) == JSON_SYNTAX);
    CHECK(parse("{", &s) == JSON_SYNTAX);
    CHECK(parse("{\"a\": 1", &s) == JSON_SYNTAX);
    CHECK(parse("{\"a\" 1}", &s) == JSON_SYNTAX);
    CHECK(parse("{\"a\": 1,}", &s) == JSON_SYNTAX);               // Trailing comma
    CHECK(parse("{\"a\": 1 \"b\": 2}", &s) == JSON_SYNTAX);       // Missing comma
    CHECK(parse("{a: 1}", &s) == JSON_SYNTAX);                    // Unquoted key
    CHECK(parse("{\"a\": {}}", &s) == JSON_SYNTAX);               // Nested: bodies are flat
    CHECK(parse("{\"a\": [1]}", &s) == JSON_SYNTAX);
    CHECK(parse("{\"a\": tru}", &s) == JSON_SYNTAX);
    CHECK(parse("{\"a\": truex}", &s) == JSON_SYNTAX);
    CHECK(parse("{} {}", &s) == JSON_SYNTAX);                     // Trailing text
    CHECK(parse("{\"a\": 1}x", &s) == JSON_SYNTAX);

    char key[JSON_KEY_MAX + 2];
    char text[JSON_KEY_MAX + 16];
    memset(key, 'k', JSON_KEY_MAX);
    key[JSON_KEY_MAX] = '\0';
    snprintf(text, sizeof(text), "{\"%s\": 1}", key);
    CHECK(parse(text, &s) == JSON_OK && strlen(s.keys[0]) == JSON_KEY_MAX);
    key[JSON_KEY_MAX] = 'k';
    key[JSON_KEY_MAX + 1] = '\0';
    snprintf(text, sizeof(text), "{\"%s\": 1}", key);
    CHECK(parse(text, &s) == JSON_SYNTAX);                        // Key too long

    // The length counts, not a NUL: a body cut short is incomplete, a NUL inside it is bad syntax
    CHECK(json_parse_object("{\"a\": 1}", 6, collect, &s) == JSON_SYNTAX);
    CHECK(json_parse_object("{\"a\": 1}\0", 9, collect, &s) == JSON_SYNTAX);
}

static void test_reader_stop(void) {
    seen_t s;
    memset(&s, 0, sizeof(s));
    s.stop_at = 2;
    const char *text = "{\"a\": 1, \"b\": 2, \"c\": 3}";
    CHECK(json_parse_object(text, strlen(text), collect, &s) == JSON_STOPPED);
    CHECK(s.count == 2);
}

int main(void) {
    test_writer();
    test_writer_newline_escape();
    test_writer_overflow_and_nesting();
    test_reader_members();
    test_reader_strings();
    test_reader_numbers();
    test_reader_rejects();
    test_reader_stop();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("json: all tests passed\n");
    return 0;
}
