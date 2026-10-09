/*
 * routes/route_static.c — Serve static files from www/ directory.
 */
#include "route_static.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Resolve MIME type from file extension */
static const char *mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    return "application/octet-stream";
}

void route_static(HttpRequest *req, HttpResponse *resp) {
    char filepath[2048];
    snprintf(filepath, sizeof(filepath), "c_server/www%s", req->path);

    /* Security: reject path traversal */
    if (strstr(filepath, "..")) {
        http_respond_text(resp, 403, "text/plain", "Forbidden");
        return;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        snprintf(filepath, sizeof(filepath), "www%s", req->path);
        f = fopen(filepath, "rb");
    }
    if (!f) {
        http_respond_text(resp, 404, "text/plain", "File Not Found");
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    char *buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); http_respond_text(resp, 500, "text/plain", "OOM"); return; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    resp->status_code = 200;
    strncpy(resp->content_type, mime_type(filepath), sizeof(resp->content_type)-1);
    resp->body     = buf;  /* ownership passed to resp */
    resp->body_len = (int)sz;
}
