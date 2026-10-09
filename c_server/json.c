/*
 * json.c — Lightweight JSON builder implementation.
 */
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ── Internal helpers ───────────────────────────────────────────────── */

static void jappend(JsonBuf *j, const char *s) {
    size_t left = j->size - j->len - 1;
    size_t n = strlen(s);
    if (n > left) n = left;
    if (n == 0) return;
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

static void jappend_char(JsonBuf *j, char c) {
    if (j->len + 1 >= j->size) return;
    j->buf[j->len++] = c;
    j->buf[j->len] = '\0';
}

static void jappend_fmt(JsonBuf *j, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    jappend(j, tmp);
}

/* Escape a string for JSON – writes opening ", content, closing " */
static void jescaped_str(JsonBuf *j, const char *s) {
    jappend_char(j, '"');
    for (; *s; s++) {
        switch (*s) {
            case '"':  jappend(j, "\\\""); break;
            case '\\': jappend(j, "\\\\"); break;
            case '\n': jappend(j, "\\n");  break;
            case '\r': jappend(j, "\\r");  break;
            case '\t': jappend(j, "\\t");  break;
            default:
                if ((unsigned char)*s < 0x20) {
                    jappend_fmt(j, "\\u%04x", (unsigned char)*s);
                } else {
                    jappend_char(j, *s);
                }
                break;
        }
    }
    jappend_char(j, '"');
}

/* ── Public API ─────────────────────────────────────────────────────── */

void json_init(JsonBuf *j, char *buf, size_t size) {
    j->buf  = buf;
    j->size = size;
    j->len  = 0;
    if (size > 0) buf[0] = '\0';
}

void json_obj_open(JsonBuf *j)  { jappend_char(j, '{'); }
void json_obj_close(JsonBuf *j) { jappend_char(j, '}'); }
void json_arr_open(JsonBuf *j)  { jappend_char(j, '['); }
void json_arr_close(JsonBuf *j) { jappend_char(j, ']'); }
void json_comma(JsonBuf *j)     { jappend_char(j, ','); }

void json_key(JsonBuf *j, const char *key) {
    jescaped_str(j, key);
    jappend_char(j, ':');
}

void json_str(JsonBuf *j, const char *val)  { jescaped_str(j, val); }
void json_int(JsonBuf *j, long long val)    { jappend_fmt(j, "%lld", val); }
void json_bool(JsonBuf *j, int val)         { jappend(j, val ? "true" : "false"); }
void json_null(JsonBuf *j)                  { jappend(j, "null"); }

void json_double(JsonBuf *j, double val, int decimals) {
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    jappend_fmt(j, fmt, val);
}

/* ── Convenience: key + value ───────────────────────────────────────── */

void json_kv_str(JsonBuf *j, const char *key, const char *val) {
    json_key(j, key); json_str(j, val);
}
void json_kv_int(JsonBuf *j, const char *key, long long val) {
    json_key(j, key); json_int(j, val);
}
void json_kv_double(JsonBuf *j, const char *key, double val, int dec) {
    json_key(j, key); json_double(j, val, dec);
}
void json_kv_bool(JsonBuf *j, const char *key, int val) {
    json_key(j, key); json_bool(j, val);
}
void json_kv_null(JsonBuf *j, const char *key) {
    json_key(j, key); json_null(j);
}
