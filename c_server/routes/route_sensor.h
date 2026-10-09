/* routes/route_sensor.h */
#ifndef ROUTE_SENSOR_H
#define ROUTE_SENSOR_H
#include "../http_server.h"
void route_update_sensor(HttpRequest *req, HttpResponse *resp);
void route_get_sensor_data(HttpRequest *req, HttpResponse *resp);
#endif
