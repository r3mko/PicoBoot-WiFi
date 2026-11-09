/**
 * Copyright (c) 2026 Remko Kleinjan
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>
#include "json.h"

// --- Writer ---------------------------------------------------------------------------------------

static void put_raw(json_out_t *j, const char *s, size_t n) {
    if (j->overflow) {
        return;
    }
    if (j->len + n >= j->size) {
        j->overflow = true;
        return;
    }
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

static void put_char(json_out_t *j, char c) {
    put_raw(j, &c, 1);
}

static void put_escaped(json_out_t *j, const char *s) {
    put_char(j, '"');
    for (; *s; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            put_char(j, '\\');
            put_char(j, (char)c);
        } else if (c < 0x20) {
            char e[8];
            snprintf(e, sizeof(e), "\\u%04x", c);
            put_raw(j, e, 6);
        } else {
            put_char(j, (char)c);
        }
    }
    put_char(j, '"');
}

// The comma and the key before a value
static void member(json_out_t *j, const char *key) {
    if (!j->first[j->depth]) {
        put_char(j, ',');
    }
    j->first[j->depth] = false;
    if (key) {
        put_escaped(j, key);
        put_char(j, ':');
    }
}

void json_init(json_out_t *j, char *buf, size_t size) {
    memset(j, 0, sizeof(*j));
    j->buf = buf;
    j->size = size;
    j->overflow = size == 0;
    j->first[0] = true;
    if (size) {
        buf[0] = '\0';
    }
}

static void open_container(json_out_t *j, const char *key, char c) {
    member(j, key);
    put_char(j, c);
    if (j->depth == JSON_DEPTH_MAX) {
        j->overflow = true;
        return;
    }
    j->depth++;
    j->first[j->depth] = true;
}

static void close_container(json_out_t *j, char c) {
    if (j->depth == 0) {
        j->overflow = true;
        return;
    }
    j->depth--;
    put_char(j, c);
}

void json_object_begin(json_out_t *j, const char *key) {
    open_container(j, key, '{');
}

void json_object_end(json_out_t *j) {
    close_container(j, '}');
}

void json_array_begin(json_out_t *j, const char *key) {
    open_container(j, key, '[');
}

void json_array_end(json_out_t *j) {
    close_container(j, ']');
}

void json_string(json_out_t *j, const char *key, const char *value) {
    member(j, key);
    put_escaped(j, value);
}

void json_int(json_out_t *j, const char *key, long value) {
    char n[24];
    member(j, key);
    put_raw(j, n, (size_t)snprintf(n, sizeof(n), "%ld", value));
}

void json_bool(json_out_t *j, const char *key, bool value) {
    member(j, key);
    put_raw(j, value ? "true" : "false", value ? 4 : 5);
}

void json_tenths(json_out_t *j, const char *key, long tenths) {
    char n[24];
    const unsigned long a = tenths < 0 ? 0ul - (unsigned long)tenths : (unsigned long)tenths;
    member(j, key);
    put_raw(j, n, (size_t)snprintf(n, sizeof(n), "%s%lu.%lu", tenths < 0 ? "-" : "", a / 10, a % 10));
}

size_t json_finish(json_out_t *j) {
    return j->overflow || j->depth != 0 ? 0 : j->len;
}

// --- Reader ---------------------------------------------------------------------------------------

typedef struct {
    const char *p, *end;
} cursor_t;

static void skip_space(cursor_t *c) {
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')) {
        c->p++;
    }
}

static bool take(cursor_t *c, char ch) {
    if (c->p < c->end && *c->p == ch) {
        c->p++;
        return true;
    }
    return false;
}

static bool take_word(cursor_t *c, const char *word) {
    const size_t n = strlen(word);
    if ((size_t)(c->end - c->p) < n || memcmp(c->p, word, n) != 0) {
        return false;
    }
    c->p += n;
    return true;
}

static bool is_digit(cursor_t *c) {
    return c->p < c->end && *c->p >= '0' && *c->p <= '9';
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// A string into out (at most max characters, then *too_long); false on bad syntax
static bool read_string(cursor_t *c, char *out, size_t max, bool *too_long) {
    size_t n = 0;
    *too_long = false;
    if (!take(c, '"')) {
        return false;
    }
    while (true) {
        if (c->p >= c->end) {
            return false;
        }
        unsigned char ch = (unsigned char)*c->p++;
        if (ch == '"') {
            break;
        }
        if (ch < 0x20) {
            return false;                   // Control characters must be escaped
        }
        if (ch == '\\') {
            if (c->p >= c->end) {
                return false;
            }
            switch (*c->p++) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    unsigned v = 0;
                    for (int i = 0; i < 4; i++) {
                        const int h = c->p < c->end ? hex_value(*c->p++) : -1;
                        if (h < 0) {
                            return false;
                        }
                        v = v * 16 + (unsigned)h;
                    }
                    // Non-ASCII and NUL become 0x80, which no setting allows: a NUL would cut the
                    // value short of what the client sent
                    ch = v == 0 || v >= 0x80 ? 0x80 : (unsigned char)v;
                    break;
                }
                default:
                    return false;
            }
        }
        if (n < max) {
            out[n++] = (char)ch;
        } else {
            *too_long = true;
        }
    }
    out[n] = '\0';
    return true;
}

static bool read_number(cursor_t *c, json_value_t *v) {
    const bool negative = take(c, '-');
    long n = 0;
    unsigned digits = 0;
    bool integer = true;
    if (!is_digit(c)) {
        return false;
    }
    if (take(c, '0')) {
        digits = 1;                         // No leading zeros: a digit after this is a syntax error
    } else {
        while (is_digit(c)) {
            if (digits < 9) {
                n = n * 10 + (*c->p - '0');
            }
            digits++;
            c->p++;
        }
    }
    if (digits > 9) {
        integer = false;
    }
    if (take(c, '.')) {
        if (!is_digit(c)) {
            return false;
        }
        while (is_digit(c)) {
            c->p++;
        }
        integer = false;
    }
    if (take(c, 'e') || take(c, 'E')) {
        if (!take(c, '+')) {
            take(c, '-');
        }
        if (!is_digit(c)) {
            return false;
        }
        while (is_digit(c)) {
            c->p++;
        }
        integer = false;
    }
    v->type = JSON_NUMBER;
    v->integer = integer;
    v->number = integer ? (negative ? -n : n) : 0;
    return true;
}

json_result_t json_parse_object(const char *text, size_t len, json_member_fn fn, void *ctx) {
    cursor_t c = {text, text + len};
    skip_space(&c);
    if (!take(&c, '{')) {
        return JSON_SYNTAX;
    }
    skip_space(&c);
    if (!take(&c, '}')) {
        while (true) {
            char key[JSON_KEY_MAX + 1];
            bool key_too_long;
            json_value_t v;
            memset(&v, 0, sizeof(v));
            skip_space(&c);
            if (!read_string(&c, key, JSON_KEY_MAX, &key_too_long) || key_too_long) {
                return JSON_SYNTAX;
            }
            skip_space(&c);
            if (!take(&c, ':')) {
                return JSON_SYNTAX;
            }
            skip_space(&c);
            if (c.p < c.end && *c.p == '"') {
                v.type = JSON_STRING;
                if (!read_string(&c, v.string, JSON_STRING_MAX, &v.too_long)) {
                    return JSON_SYNTAX;
                }
            } else if (take_word(&c, "true")) {
                v.type = JSON_BOOL;
                v.boolean = true;
            } else if (take_word(&c, "false")) {
                v.type = JSON_BOOL;
            } else if (take_word(&c, "null")) {
                v.type = JSON_NULL;
            } else if (!read_number(&c, &v)) {
                return JSON_SYNTAX;         // Also objects and arrays: the bodies are flat
            }
            if (!fn(ctx, key, &v)) {
                return JSON_STOPPED;
            }
            skip_space(&c);
            if (take(&c, '}')) {
                break;
            }
            if (!take(&c, ',')) {
                return JSON_SYNTAX;
            }
        }
    }
    skip_space(&c);
    return c.p == c.end ? JSON_OK : JSON_SYNTAX;
}
