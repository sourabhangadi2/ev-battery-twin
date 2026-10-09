/*
 * router.h — URL dispatcher mapping routes to handlers.
 */
#ifndef ROUTER_H
#define ROUTER_H

#include "http_server.h"

/* Main dispatch function called by http_server.c for every request */
void router_dispatch(HttpRequest *req, HttpResponse *resp);

/* ── Authentication gate ─────────────────────────────────────────────
 * Returns 1 if request carries a valid session cookie.
 * If not, fills resp with a redirect to /login and returns 0.
 */
int router_require_auth(const HttpRequest *req, HttpResponse *resp,
                        int *user_id_out);

#endif /* ROUTER_H */
