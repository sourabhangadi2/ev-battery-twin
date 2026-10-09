/* routes/route_auth.h */
#ifndef ROUTE_AUTH_H
#define ROUTE_AUTH_H
#include "../http_server.h"
void route_login(HttpRequest *req, HttpResponse *resp);
void route_logout(HttpRequest *req, HttpResponse *resp);
void route_register(HttpRequest *req, HttpResponse *resp);
void route_dashboard(HttpRequest *req, HttpResponse *resp, int user_id);
#endif
