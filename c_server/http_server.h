/*
 * http_server.h — Bespoke HTTP/1.1 server using Winsock2.
 * Parses requests, dispatches to router, sends responses.
 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <winsock2.h>
#include <ws2tcpip.h>

#define HTTP_MAX_HEADERS     32
#define HTTP_HEADER_BUF_SIZE (16 * 1024)   /* 16 KB for request headers */
#define HTTP_BODY_MAX_SIZE   (512 * 1024)  /* 512 KB max body */
#define HTTP_RESP_BUF_SIZE   (1024 * 1024) /* 1 MB response buffer */

/* Parsed HTTP request */
typedef struct {
    char  method[16];
    char  path[2048];
    char  query[2048];        /* query string, without '?' */
    char  headers[HTTP_HEADER_BUF_SIZE];
    int   headers_len;
    char *body;               /* heap-allocated; caller must free if non-NULL */
    int   body_len;
} HttpRequest;

/* HTTP response to send */
typedef struct {
    int   status_code;
    char  content_type[128];
    char  extra_headers[1024]; /* additional headers (Set-Cookie, Location, etc.) */
    char *body;                /* heap-allocated */
    int   body_len;
} HttpResponse;

/* Start listening on the given port. Blocks forever, dispatching requests. */
int http_server_run(int port);

/* ── Helpers for route handlers ─────────────────────────────────────── */

/* Get a request header value by name (case-insensitive). Returns NULL if absent. */
const char *http_get_header(const HttpRequest *req, const char *name);

/* Get a cookie value by name. Writes to out_buf (size out_size). Returns 1 if found. */
int http_get_cookie(const HttpRequest *req, const char *name,
                    char *out_buf, int out_size);

/* Build an HTTP response with a heap-copied body string (NUL-terminated). */
void http_respond_text(HttpResponse *resp, int code,
                       const char *content_type, const char *body);

/* Redirect response */
void http_respond_redirect(HttpResponse *resp, const char *location);

/* Free response body */
void http_response_free(HttpResponse *resp);

/* URL-decode a string in place */
void url_decode(char *s);

/* Parse application/x-www-form-urlencoded body into key=value pairs.
 * Returns number of pairs. keys[] and vals[] must be char*[max] arrays.
 * Decoded strings are stored in buf (must be body_len*2+1 bytes). */
int form_parse(const char *body, int body_len,
               char **keys, char **vals, int max,
               char *buf, int buf_size);

/* Get a form value by key (returns NULL if not found) */
const char *form_get(char **keys, char **vals, int count, const char *key);

/* Simple JSON field extractor — reads "key":"value" or "key":value from JSON body.
 * Returns 1 if found. Works for simple flat JSON only. */
int json_extract_str(const char *json, const char *key, char *out, int out_size);
int json_extract_double(const char *json, const char *key, double *out);
int json_extract_bool(const char *json, const char *key, int *out);

#endif /* HTTP_SERVER_H */
