/*
 * routes/route_sensor.c — Sensor data API routes.
 * Ports routes.py: /update_sensor_data and /get_sensor_data
 */
#include "route_sensor.h"
#include "../database.h"
#include "../twin_engine.h"
#include "../healing_engine.h"
#include "../json.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* ── POST /update_sensor_data ─────────────────────────────────────────
 * Called by ESP32 or simulation. Runs Digital Twin + Self-Healing.
 * Returns JSON: { "status":"success", "commands":{...} }
 */
void route_update_sensor(HttpRequest *req, HttpResponse *resp) {
    if (strcmp(req->method, "POST") != 0) {
        http_respond_text(resp, 405, "application/json",
            "{\"status\":\"error\",\"message\":\"Method Not Allowed\"}");
        return;
    }
    if (!req->body || req->body_len == 0) {
        http_respond_text(resp, 400, "application/json",
            "{\"status\":\"error\",\"message\":\"Missing JSON payload\"}");
        return;
    }

    double temp, humidity, voltage, current;
    if (!json_extract_double(req->body, "temperature", &temp) ||
        !json_extract_double(req->body, "humidity",    &humidity) ||
        !json_extract_double(req->body, "voltage",     &voltage) ||
        !json_extract_double(req->body, "current",     &current)) {
        http_respond_text(resp, 400, "application/json",
            "{\"status\":\"error\",\"message\":\"Invalid numeric parameter formats\"}");
        return;
    }

    double power = round(voltage * current * 1000.0) / 1000.0;

    /* Store telemetry */
    SensorData sensor = {0};
    sensor.temperature        = round(temp    * 100.0) / 100.0;
    sensor.humidity           = round(humidity * 100.0) / 100.0;
    sensor.voltage            = round(voltage  * 100.0) / 100.0;
    sensor.current            = round(current  * 1000.0) / 1000.0;
    sensor.power              = power;
    sensor.battery_percentage = 50.0;
    sensor.timestamp          = time(NULL);

    if (!db_sensor_insert(&sensor)) {
        http_respond_text(resp, 500, "application/json",
            "{\"status\":\"error\",\"message\":\"Database insert failed\"}");
        return;
    }

    /* Run Digital Twin */
    BatteryHealth health = {0};
    update_digital_twin(&sensor, &health);

    /* Run Self-Healing Engine */
    Command cmd = {0};
    run_self_healing_engine(&sensor, &cmd);

    /* Build JSON response */
    static char jbuf[512];
    JsonBuf j;
    json_init(&j, jbuf, sizeof(jbuf));
    json_obj_open(&j);
      json_kv_str(&j, "status", "success"); json_comma(&j);
      json_key(&j, "commands"); json_obj_open(&j);
        json_kv_bool(&j, "relay",       cmd.relay);       json_comma(&j);
        json_kv_bool(&j, "fan",         cmd.fan);         json_comma(&j);
        json_kv_bool(&j, "buzzer",      cmd.buzzer);      json_comma(&j);
        json_kv_bool(&j, "reset_alarm", cmd.reset_alarm);
      json_obj_close(&j);
    json_obj_close(&j);

    http_respond_text(resp, 200, "application/json", jbuf);
}

/* ── GET /get_sensor_data ─────────────────────────────────────────────
 * Returns full dashboard data: telemetry, health, commands, alerts, logs, charts.
 */
void route_get_sensor_data(HttpRequest *req, HttpResponse *resp) {
    (void)req;

    SensorData  sensor  = {0};
    BatteryHealth health = {0};
    Command cmd         = {0};

    int has_sensor = db_sensor_latest(&sensor);
    int has_health = db_health_latest(&health);
    db_command_get_or_create(&cmd);

    double efficiency = 100.0;
    if (has_sensor && has_health) {
        efficiency = get_efficiency(sensor.voltage, sensor.current, sensor.temperature);
    }

    Alert alerts[32]; int nalerts = 0;
    nalerts = db_alert_list_unresolved(alerts, 32);

    SystemLog logs[15]; int nlogs = 0;
    nlogs = db_log_list_recent(logs, 15);

    SensorData hist[20]; int nhist = 0;
    nhist = db_sensor_history(hist, 20);

    /* Build large JSON response */
    static char jbuf[65536];
    JsonBuf j;
    json_init(&j, jbuf, sizeof(jbuf));
    json_obj_open(&j);

    /* telemetry */
    json_key(&j, "telemetry");
    if (has_sensor) {
        char ts[32];
        struct tm *tm_info = gmtime(&sensor.timestamp);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
        json_obj_open(&j);
          json_kv_double(&j, "temperature",       sensor.temperature,        1); json_comma(&j);
          json_kv_double(&j, "humidity",           sensor.humidity,           1); json_comma(&j);
          json_kv_double(&j, "voltage",            sensor.voltage,            2); json_comma(&j);
          json_kv_double(&j, "current",            sensor.current,            3); json_comma(&j);
          json_kv_double(&j, "power",              sensor.power,              3); json_comma(&j);
          json_kv_double(&j, "battery_percentage", sensor.battery_percentage, 1); json_comma(&j);
          json_kv_str(&j,   "timestamp",           ts);
        json_obj_close(&j);
    } else { json_null(&j); }
    json_comma(&j);

    /* health */
    json_key(&j, "health");
    if (has_health) {
        char ts[32];
        struct tm *tm_info = gmtime(&health.timestamp);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
        json_obj_open(&j);
          json_kv_double(&j, "health_score",             health.health_score,             2); json_comma(&j);
          json_kv_double(&j, "degradation",              health.degradation,              2); json_comma(&j);
          json_kv_int(&j,   "estimated_cycles",          (long long)health.estimated_cycles); json_comma(&j);
          json_kv_int(&j,   "estimated_remaining_life",  (long long)health.estimated_remaining_life); json_comma(&j);
          json_kv_double(&j, "efficiency",               round(efficiency*10.0)/10.0,     1); json_comma(&j);
          json_kv_str(&j,   "timestamp",                 ts);
        json_obj_close(&j);
    } else { json_null(&j); }
    json_comma(&j);

    /* commands */
    json_key(&j, "commands");
    json_obj_open(&j);
      json_kv_bool(&j, "relay",       cmd.relay);       json_comma(&j);
      json_kv_bool(&j, "fan",         cmd.fan);         json_comma(&j);
      json_kv_bool(&j, "buzzer",      cmd.buzzer);      json_comma(&j);
      json_kv_bool(&j, "reset_alarm", cmd.reset_alarm); json_comma(&j);
      json_kv_str(&j,  "fan_mode",    cmd.fan_mode);    json_comma(&j);
      json_kv_str(&j,  "relay_mode",  cmd.relay_mode);
    json_obj_close(&j);
    json_comma(&j);

    /* alerts */
    json_key(&j, "alerts"); json_arr_open(&j);
    for (int i = 0; i < nalerts; i++) {
        char ts[32];
        struct tm *tm_info = gmtime(&alerts[i].timestamp);
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", tm_info);
        if (i > 0) json_comma(&j);
        json_obj_open(&j);
          json_kv_int(&j, "id",         (long long)alerts[i].id); json_comma(&j);
          json_kv_str(&j, "alert_type", alerts[i].alert_type);    json_comma(&j);
          json_kv_str(&j, "severity",   alerts[i].severity);      json_comma(&j);
          json_kv_str(&j, "message",    alerts[i].message);       json_comma(&j);
          json_kv_str(&j, "timestamp",  ts);                      json_comma(&j);
          json_kv_bool(&j,"resolved",   alerts[i].resolved);
        json_obj_close(&j);
    }
    json_arr_close(&j);
    json_comma(&j);

    /* logs */
    json_key(&j, "logs"); json_arr_open(&j);
    for (int i = 0; i < nlogs; i++) {
        char ts[32];
        struct tm *tm_info = gmtime(&logs[i].timestamp);
        strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", tm_info);
        if (i > 0) json_comma(&j);
        json_obj_open(&j);
          json_kv_int(&j, "id",          (long long)logs[i].id); json_comma(&j);
          json_kv_str(&j, "event",       logs[i].event);         json_comma(&j);
          json_kv_str(&j, "description", logs[i].description);   json_comma(&j);
          json_kv_str(&j, "timestamp",   ts);
        json_obj_close(&j);
    }
    json_arr_close(&j);
    json_comma(&j);

    /* chart_data */
    json_key(&j, "chart_data"); json_obj_open(&j);
      json_key(&j, "timestamps"); json_arr_open(&j);
      for (int i = 0; i < nhist; i++) {
          char ts[16]; struct tm *t = gmtime(&hist[i].timestamp);
          strftime(ts, sizeof(ts), "%H:%M:%S", t);
          if (i > 0) { json_comma(&j); }
          json_str(&j, ts);
      }
      json_arr_close(&j); json_comma(&j);

      json_key(&j,"temperature"); json_arr_open(&j);
      for(int i=0;i<nhist;i++){if(i>0)json_comma(&j);json_double(&j,hist[i].temperature,1);}
      json_arr_close(&j); json_comma(&j);

      json_key(&j,"voltage"); json_arr_open(&j);
      for(int i=0;i<nhist;i++){if(i>0)json_comma(&j);json_double(&j,hist[i].voltage,2);}
      json_arr_close(&j); json_comma(&j);

      json_key(&j,"current"); json_arr_open(&j);
      for(int i=0;i<nhist;i++){if(i>0)json_comma(&j);json_double(&j,hist[i].current,3);}
      json_arr_close(&j); json_comma(&j);

      json_key(&j,"power"); json_arr_open(&j);
      for(int i=0;i<nhist;i++){if(i>0)json_comma(&j);json_double(&j,hist[i].power,3);}
      json_arr_close(&j); json_comma(&j);

      json_key(&j,"soc"); json_arr_open(&j);
      for(int i=0;i<nhist;i++){if(i>0)json_comma(&j);json_double(&j,hist[i].battery_percentage,1);}
      json_arr_close(&j);
    json_obj_close(&j);

    json_obj_close(&j);

    http_respond_text(resp, 200, "application/json", jbuf);
}
