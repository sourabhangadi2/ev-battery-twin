/* routes/route_command.h */
#ifndef ROUTE_COMMAND_H
#define ROUTE_COMMAND_H
#include "../http_server.h"
void route_send_command(HttpRequest *req, HttpResponse *resp);
void route_get_commands(HttpRequest *req, HttpResponse *resp);
#endif
