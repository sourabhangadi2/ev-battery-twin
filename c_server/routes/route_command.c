/*
 * routes/route_command.c — Command API routes.
 * Ports routes.py: /send_command and /get_commands
 */
#include "route_command.h"
#include "../database.h"
#include "../json.h"
#include <string.h>
#include <stdio.h>

/* ── POST /send_command ───────────────────────────────────────────────
 * Accepts manual override commands from the dashboard.
 */
void route_send_command(HttpRequest *req, HttpResponse *resp) {
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

    char device[64] = {0};
    if (!json_extract_str(req->body, "device", device, sizeof(device))) {
        http_respond_text(resp, 400, "application/json",
            "{\"status\":\"error\",\"message\":\"Missing device field\"}");
        return;
    }

    /* Validate device name */
    const char *valid[] = {"relay_mode","fan_mode","buzzer","reset_alarm","relay","fan", NULL};
    int valid_device = 0;
    for (int i = 0; valid[i]; i++) {
        if (strcmp(device, valid[i]) == 0) { valid_device = 1; break; }
    }
    if (!valid_device) {
        http_respond_text(resp, 400, "application/json",
            "{\"status\":\"error\",\"message\":\"Invalid device command request\"}");
        return;
    }

    Command cmd = {0};
    db_command_get_or_create(&cmd);

    char event_desc[512] = {0};

    if (strcmp(device, "relay_mode") == 0) {
        char val[16] = {0};
        json_extract_str(req->body, "value", val, sizeof(val));
        if (strcmp(val,"auto")!=0 && strcmp(val,"on")!=0 && strcmp(val,"off")!=0) {
            http_respond_text(resp, 400, "application/json",
                "{\"status\":\"error\",\"message\":\"Invalid value for relay_mode\"}");
            return;
        }
        strncpy(cmd.relay_mode, val, sizeof(cmd.relay_mode)-1);
        if (strcmp(val,"on")==0)  cmd.relay = 1;
        if (strcmp(val,"off")==0) cmd.relay = 0;
        snprintf(event_desc, sizeof(event_desc), "Operator override: Set relay mode to %s.", val);

    } else if (strcmp(device, "fan_mode") == 0) {
        char val[16] = {0};
        json_extract_str(req->body, "value", val, sizeof(val));
        if (strcmp(val,"auto")!=0 && strcmp(val,"on")!=0 && strcmp(val,"off")!=0) {
            http_respond_text(resp, 400, "application/json",
                "{\"status\":\"error\",\"message\":\"Invalid value for fan_mode\"}");
            return;
        }
        strncpy(cmd.fan_mode, val, sizeof(cmd.fan_mode)-1);
        if (strcmp(val,"on")==0)  cmd.fan = 1;
        if (strcmp(val,"off")==0) cmd.fan = 0;
        snprintf(event_desc, sizeof(event_desc), "Operator override: Set fan mode to %s.", val);

    } else if (strcmp(device, "relay") == 0) {
        int val = 0;
        if (!json_extract_bool(req->body, "value", &val)) {
            http_respond_text(resp, 400, "application/json",
                "{\"status\":\"error\",\"message\":\"Value must be boolean\"}");
            return;
        }
        cmd.relay = val;
        strncpy(cmd.relay_mode, val ? "on" : "off", sizeof(cmd.relay_mode)-1);
        snprintf(event_desc, sizeof(event_desc), "Operator override: Relay toggled manually to %s.", val ? "true" : "false");

    } else if (strcmp(device, "fan") == 0) {
        int val = 0;
        if (!json_extract_bool(req->body, "value", &val)) {
            http_respond_text(resp, 400, "application/json",
                "{\"status\":\"error\",\"message\":\"Value must be boolean\"}");
            return;
        }
        cmd.fan = val;
        strncpy(cmd.fan_mode, val ? "on" : "off", sizeof(cmd.fan_mode)-1);
        snprintf(event_desc, sizeof(event_desc), "Operator override: Fan toggled manually to %s.", val ? "true" : "false");

    } else if (strcmp(device, "buzzer") == 0) {
        int val = 0;
        if (!json_extract_bool(req->body, "value", &val)) {
            http_respond_text(resp, 400, "application/json",
                "{\"status\":\"error\",\"message\":\"Value must be boolean\"}");
            return;
        }
        cmd.buzzer = val;
        snprintf(event_desc, sizeof(event_desc), "Operator override: Active alarm buzzer toggled to %s.", val ? "ON" : "OFF");

    } else if (strcmp(device, "reset_alarm") == 0) {
        int val = 0;
        if (!json_extract_bool(req->body, "value", &val)) {
            http_respond_text(resp, 400, "application/json",
                "{\"status\":\"error\",\"message\":\"Value must be boolean\"}");
            return;
        }
        cmd.reset_alarm = val;
        snprintf(event_desc, sizeof(event_desc), "Operator override: Safety warning indicator reset command triggered.");
    }

    db_command_update(&cmd);
    db_log_insert("COMMAND_MANUAL", event_desc);

    /* Build response */
    char jbuf[256];
    snprintf(jbuf, sizeof(jbuf),
        "{\"status\":\"success\",\"message\":\"%s state command updated successfully.\"}", device);
    http_respond_text(resp, 200, "application/json", jbuf);
}

/* ── GET /get_commands ────────────────────────────────────────────────
 * Called by ESP32 to poll current actuator state.
 */
void route_get_commands(HttpRequest *req, HttpResponse *resp) {
    (void)req;
    Command cmd = {0};
    db_command_get_or_create(&cmd);

    static char jbuf[256];
    JsonBuf j;
    json_init(&j, jbuf, sizeof(jbuf));
    json_obj_open(&j);
      json_kv_bool(&j, "relay",       cmd.relay);       json_comma(&j);
      json_kv_bool(&j, "fan",         cmd.fan);         json_comma(&j);
      json_kv_bool(&j, "buzzer",      cmd.buzzer);      json_comma(&j);
      json_kv_bool(&j, "reset_alarm", cmd.reset_alarm);
    json_obj_close(&j);

    http_respond_text(resp, 200, "application/json", jbuf);
}
