/*
 * http_server.c — Bespoke HTTP/1.1 server using Winsock2 (Windows).
 */
#define _WIN32_WINNT 0x0600
#include "http_server.h"
#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <winsock2.h>
#include <ws2tcpip.h>

/* ── JSON extractor ─────────────────────────────────────────────────── */

static const char *_find_json_value(const char *json, const char *key) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

int json_extract_str(const char *json, const char *key, char *out, int out_size) {
    const char *v = _find_json_value(json, key);
    if (!v || *v != '"') return 0;
    v++;
    int i = 0;
    while (*v && *v != '"' && i < out_size-1) {
        if (*v == '\\' && *(v+1)) { v++; }
        out[i++] = *v++;
    }
    out[i] = '\0';
    return 1;
}

int json_extract_double(const char *json, const char *key, double *out) {
    const char *v = _find_json_value(json, key);
    if (!v) return 0;
    char *end;
    *out = strtod(v, &end);
    return (end > v);
}

int json_extract_bool(const char *json, const char *key, int *out) {
    const char *v = _find_json_value(json, key);
    if (!v) return 0;
    if (strncmp(v,"true",4)==0)  { *out=1; return 1; }
    if (strncmp(v,"false",5)==0) { *out=0; return 1; }
    return 0;
}

/* ── URL decode ─────────────────────────────────────────────────────── */

void url_decode(char *s) {
    char *src = s, *dst = s;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char h[3] = {src[1], src[2], 0};
            *dst++ = (char)strtol(h, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' '; src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ── Form parsing ───────────────────────────────────────────────────── */

int form_parse(const char *body, int body_len,
               char **keys, char **vals, int max,
               char *buf, int buf_size) {
    if (!body || body_len <= 0 || buf_size < body_len+1) return 0;
    memcpy(buf, body, body_len);
    buf[body_len] = '\0';

    int count = 0;
    char *tok = buf;
    while (*tok && count < max) {
        char *amp = strchr(tok, '&');
        if (amp) *amp = '\0';
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            url_decode(tok);
            url_decode(eq+1);
            keys[count] = tok;
            vals[count] = eq+1;
            count++;
        }
        if (!amp) break;
        tok = amp+1;
    }
    return count;
}

const char *form_get(char **keys, char **vals, int count, const char *key) {
    for (int i = 0; i < count; i++) {
        if (strcmp(keys[i], key) == 0) return vals[i];
    }
    return NULL;
}

/* ── Header helpers ─────────────────────────────────────────────────── */

const char *http_get_header(const HttpRequest *req, const char *name) {
    const char *p = req->headers;
    size_t nlen = strlen(name);
    while (*p) {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            while (*p == ' ') p++;
            return p;
        }
        /* Advance to next header line */
        const char *eol = strchr(p, '\n');
        if (!eol) break;
        p = eol + 1;
    }
    return NULL;
}

int http_get_cookie(const HttpRequest *req, const char *name,
                    char *out_buf, int out_size) {
    const char *cookie_hdr = http_get_header(req, "Cookie");
    if (!cookie_hdr) return 0;
    size_t nlen = strlen(name);
    const char *p = cookie_hdr;
    while (*p) {
        while (*p == ' ') p++;
        if (strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            p += nlen + 1;
            int i = 0;
            while (*p && *p != ';' && *p != '\r' && *p != '\n' && i < out_size-1) {
                out_buf[i++] = *p++;
            }
            out_buf[i] = '\0';
            return 1;
        }
        /* Skip to next cookie */
        const char *semi = strchr(p, ';');
        if (!semi) break;
        p = semi + 1;
    }
    return 0;
}

/* ── Response helpers ───────────────────────────────────────────────── */

void http_respond_text(HttpResponse *resp, int code,
                       const char *content_type, const char *body) {
    resp->status_code = code;
    strncpy(resp->content_type, content_type, sizeof(resp->content_type)-1);
    int len = body ? (int)strlen(body) : 0;
    resp->body = body ? (char*)malloc(len+1) : NULL;
    if (resp->body) { memcpy(resp->body, body, len+1); }
    resp->body_len = len;
}

void http_respond_redirect(HttpResponse *resp, const char *location) {
    resp->status_code = 302;
    strncpy(resp->content_type, "text/plain", sizeof(resp->content_type)-1);
    snprintf(resp->extra_headers, sizeof(resp->extra_headers),
             "Location: %s\r\n", location);
    resp->body     = strdup("Redirecting...");
    resp->body_len = (int)strlen(resp->body);
}

void http_response_free(HttpResponse *resp) {
    free(resp->body);
    resp->body = NULL;
}

/* ── Status text ────────────────────────────────────────────────────── */

static const char *status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

/* ── Socket send helpers ────────────────────────────────────────────── */

static void send_all(SOCKET sock, const char *data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) break;
        sent += n;
    }
}

/* ── Read exact N bytes from socket ────────────────────────────────── */

static int recv_exact(SOCKET sock, char *buf, int n, int timeout_ms) {
    int got = 0;
    while (got < n) {
        fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
        struct timeval tv = {timeout_ms/1000, (timeout_ms%1000)*1000};
        int s = select(0, &fds, NULL, NULL, &tv);
        if (s <= 0) break;
        int r = recv(sock, buf + got, n - got, 0);
        if (r <= 0) break;
        got += r;
    }
    return got;
}

/* ── Handle a single connection ─────────────────────────────────────── */

static void handle_connection(SOCKET client) {
    /* Read request line + headers */
    static __thread char raw[HTTP_HEADER_BUF_SIZE];
    int total = 0;
    char *header_end = NULL;
    int timeout_ms = 5000;

    /* Read until we find \r\n\r\n */
    while (total < HTTP_HEADER_BUF_SIZE - 1) {
        fd_set fds; FD_ZERO(&fds); FD_SET(client, &fds);
        struct timeval tv = {timeout_ms/1000, (timeout_ms%1000)*1000};
        if (select(0, &fds, NULL, NULL, &tv) <= 0) break;
        int n = recv(client, raw + total, HTTP_HEADER_BUF_SIZE - total - 1, 0);
        if (n <= 0) break;
        total += n;
        raw[total] = '\0';
        header_end = strstr(raw, "\r\n\r\n");
        if (header_end) break;
    }
    if (!header_end) { closesocket(client); return; }
    *header_end = '\0';
    int body_start_offset = (int)(header_end - raw) + 4; /* skip \r\n\r\n */

    /* Parse request line */
    HttpRequest req;
    memset(&req, 0, sizeof(req));

    char *line_end = strchr(raw, '\r');
    if (!line_end) { closesocket(client); return; }
    char req_line[2048]; int rll = (int)(line_end - raw);
    if (rll >= (int)sizeof(req_line)) rll = sizeof(req_line)-1;
    memcpy(req_line, raw, rll); req_line[rll] = '\0';

    /* "METHOD /path HTTP/1.1" */
    char path_and_query[2048];
    if (sscanf(req_line, "%15s %2047s", req.method, path_and_query) < 2) {
        closesocket(client); return;
    }
    /* Split path and query */
    char *q = strchr(path_and_query, '?');
    if (q) {
        *q = '\0';
        strncpy(req.query, q+1, sizeof(req.query)-1);
    }
    strncpy(req.path, path_and_query, sizeof(req.path)-1);
    url_decode(req.path);

    /* Copy headers (all lines after request line) */
    const char *hstart = raw + rll + 2; /* skip first \r\n */
    strncpy(req.headers, hstart, sizeof(req.headers)-1);

    /* Read body if Content-Length present */
    const char *cl_str = http_get_header(&req, "Content-Length");
    int content_length = cl_str ? atoi(cl_str) : 0;
    if (content_length > HTTP_BODY_MAX_SIZE) content_length = HTTP_BODY_MAX_SIZE;

    if (content_length > 0) {
        req.body = (char*)malloc(content_length + 1);
        if (req.body) {
            /* Copy whatever body bytes already arrived */
            int already = total - body_start_offset;
            if (already > content_length) already = content_length;
            if (already > 0) memcpy(req.body, raw + body_start_offset, already);
            /* Read remaining bytes */
            int remaining = content_length - already;
            if (remaining > 0) recv_exact(client, req.body + already, remaining, 5000);
            req.body[content_length] = '\0';
            req.body_len = content_length;
        }
    }

    /* Dispatch to router */
    HttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    router_dispatch(&req, &resp);

    /* Send HTTP response */
    char head[1024];
    int hlen = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s; charset=utf-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        resp.status_code, status_text(resp.status_code),
        resp.content_type[0] ? resp.content_type : "text/plain",
        resp.body_len,
        resp.extra_headers);
    send_all(client, head, hlen);
    if (resp.body && resp.body_len > 0)
        send_all(client, resp.body, resp.body_len);

    /* Cleanup */
    http_response_free(&resp);
    free(req.body);
    closesocket(client);
}

/* ── Main server loop ────────────────────────────────────────────────── */

int http_server_run(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 0;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        return 0;
    }

    /* Allow address reuse */
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)port);

    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        closesocket(server); return 0;
    }
    if (listen(server, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        closesocket(server); return 0;
    }

    printf("Self-Healing EV Battery Simulator (C Backend) running on http://127.0.0.1:%d\n", port);
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client = accept(server, (struct sockaddr*)&client_addr, &addr_len);
        if (client == INVALID_SOCKET) continue;
        handle_connection(client);
    }

    closesocket(server);
    WSACleanup();
    return 1;
}
