/*
 * json.h — Lightweight JSON builder for HTTP API responses.
 * Builds JSON strings into a caller-supplied buffer.
 */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

/* Append context – tracks buffer pointer and remaining space */
typedef struct {
    char  *buf;
    size_t size;     /* total buffer size */
    size_t len;      /* bytes written so far (excluding NUL) */
} JsonBuf;

void json_init(JsonBuf *j, char *buf, size_t size);
void json_obj_open(JsonBuf *j);
void json_obj_close(JsonBuf *j);
void json_arr_open(JsonBuf *j);
void json_arr_close(JsonBuf *j);
void json_key(JsonBuf *j, const char *key);
void json_str(JsonBuf *j, const char *val);
void json_int(JsonBuf *j, long long val);
void json_double(JsonBuf *j, double val, int decimals);
void json_bool(JsonBuf *j, int val);
void json_null(JsonBuf *j);
void json_comma(JsonBuf *j);

/* Convenience: key + value in one call */
void json_kv_str(JsonBuf *j, const char *key, const char *val);
void json_kv_int(JsonBuf *j, const char *key, long long val);
void json_kv_double(JsonBuf *j, const char *key, double val, int decimals);
void json_kv_bool(JsonBuf *j, const char *key, int val);
void json_kv_null(JsonBuf *j, const char *key);

#endif /* JSON_H */
