/*
 * router.c — URL dispatcher and auth gate.
 */
#include "router.h"
#include "database.h"
#include "routes/route_auth.h"
#include "routes/route_sensor.h"
#include "routes/route_command.h"
#include "routes/route_static.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ── Auth gate ──────────────────────────────────────────────────────── */

int router_require_auth(const HttpRequest *req, HttpResponse *resp,
                        int *user_id_out) {
    char token[256] = {0};
    if (!http_get_cookie(req, "session", token, sizeof(token))) {
        http_respond_redirect(resp, "/login");
        return 0;
    }

    Session sess;
    if (!db_session_find(token, &sess)) {
        http_respond_redirect(resp, "/login");
        return 0;
    }
    if (sess.expires_at < time(NULL)) {
        db_session_delete(token);
        http_respond_redirect(resp, "/login");
        return 0;
    }

    if (user_id_out) *user_id_out = sess.user_id;
    return 1;
}

/* ── Dispatcher ─────────────────────────────────────────────────────── */

void router_dispatch(HttpRequest *req, HttpResponse *resp) {
    const char *path = req->path;

    /* ── Root redirect ── */
    if (strcmp(path, "/") == 0) {
        char tok[256];
        if (http_get_cookie(req, "session", tok, sizeof(tok))) {
            http_respond_redirect(resp, "/dashboard");
        } else {
            http_respond_redirect(resp, "/login");
        }
        return;
    }

    /* ── Auth routes ── */
    if (strcmp(path, "/login") == 0) {
        route_login(req, resp); return;
    }
    if (strcmp(path, "/logout") == 0) {
        route_logout(req, resp); return;
    }
    if (strcmp(path, "/register") == 0) {
        route_register(req, resp); return;
    }

    /* ── Dashboard (login required) ── */
    if (strcmp(path, "/dashboard") == 0) {
        int uid;
        if (!router_require_auth(req, resp, &uid)) return;
        route_dashboard(req, resp, uid); return;
    }

    /* ── Sensor API ── */
    if (strcmp(path, "/update_sensor_data") == 0) {
        route_update_sensor(req, resp); return;
    }
    if (strcmp(path, "/get_sensor_data") == 0) {
        int uid;
        if (!router_require_auth(req, resp, &uid)) return;
        route_get_sensor_data(req, resp); return;
    }

    /* ── Command API ── */
    if (strcmp(path, "/send_command") == 0) {
        int uid;
        if (!router_require_auth(req, resp, &uid)) return;
        route_send_command(req, resp); return;
    }
    if (strcmp(path, "/get_commands") == 0) {
        route_get_commands(req, resp); return;
    }

    /* ── Static files ── */
    if (strncmp(path, "/static/", 8) == 0) {
        route_static(req, resp); return;
    }

    /* ── 404 ── */
    http_respond_text(resp, 404, "text/plain", "404 Not Found");
}
