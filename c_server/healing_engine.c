/*
 * healing_engine.c — Self-Healing safety state machine.
 * Ports app/healing_engine.py verbatim to C — all 6 safety checks preserved.
 */
#include "healing_engine.h"
#include "database.h"
#include <stdio.h>
#include <string.h>

/* Safety threshold definitions (identical to Python) */
#define TEMP_WARNING_C        45.0
#define TEMP_CRITICAL_C       55.0
#define VOLTAGE_CRITICAL_HIGH  4.25
#define VOLTAGE_CRITICAL_LOW   2.90
#define CURRENT_CRITICAL_HIGH  5.0

/* Hysteresis recovery thresholds */
#define TEMP_RECOVERY_C        40.0
#define VOLTAGE_RECOVERY_LOW   3.00
#define VOLTAGE_RECOVERY_HIGH  4.20
#define CURRENT_RECOVERY       4.50

/* ── Internal helpers ───────────────────────────────────────────────── */

static void _create_alert_if_not_exists(const char *alert_type,
                                        const char *severity,
                                        const char *message) {
    Alert existing;
    if (!db_alert_find_unresolved(alert_type, severity, &existing)) {
        Alert a;
        memset(&a, 0, sizeof(a));
        strncpy(a.alert_type, alert_type, sizeof(a.alert_type)-1);
        strncpy(a.severity,   severity,   sizeof(a.severity)-1);
        strncpy(a.message,    message,    sizeof(a.message)-1);
        db_alert_insert(&a);
    }
}

static void _create_system_log(const char *event, const char *description) {
    /* Prevent duplicate consecutive identical logs */
    SystemLog last;
    if (db_log_latest(&last)) {
        if (strcmp(last.event, event) == 0 &&
            strcmp(last.description, description) == 0) {
            return;
        }
    }
    db_log_insert(event, description);
}

/* ── Main engine ────────────────────────────────────────────────────── */

int run_self_healing_engine(const SensorData *sensor, Command *cmd_out) {
    /* Fetch singleton command row */
    db_command_get_or_create(cmd_out);

    double temp    = sensor->temperature;
    double voltage = sensor->voltage;
    double current = sensor->current;

    int critical_triggered = 0;
    char msg[512];

    /* ── Check 1: Over-Temperature Critical ── */
    if (temp > TEMP_CRITICAL_C) {
        critical_triggered = 1;
        cmd_out->fan    = 1;
        cmd_out->buzzer = 1;
        cmd_out->relay  = 0;
        snprintf(msg, sizeof(msg),
            "CRITICAL: Temperature reached %.1f\xC2\xB0""C (Limit: %.1f\xC2\xB0""C). Coolant active. Battery isolated.",
            temp, TEMP_CRITICAL_C);
        _create_alert_if_not_exists("Over-Temperature", "CRITICAL", msg);
        _create_system_log("HEALING_ACTION", msg);
    }
    /* ── Check 2: Over-Temperature Warning ── */
    else if (temp > TEMP_WARNING_C) {
        cmd_out->fan = 1;
        snprintf(msg, sizeof(msg),
            "WARNING: Temperature is high at %.1f\xC2\xB0""C (Limit: %.1f\xC2\xB0""C). Cooling fan turned ON (High Speed).",
            temp, TEMP_WARNING_C);
        _create_alert_if_not_exists("High-Temperature", "WARNING", msg);
        _create_system_log("HEALING_ACTION", msg);
    }
    /* ── Check 2b: Elevated Temperature — Proportional Cooling ── */
    else if (temp > 35.0) {
        cmd_out->fan = 1;
    }
    /* ── Normal temperature — fan off ── */
    else {
        cmd_out->fan = 0;
    }

    /* ── Check 3: Over-Voltage ── */
    if (voltage > VOLTAGE_CRITICAL_HIGH) {
        critical_triggered = 1;
        cmd_out->relay  = 0;
        cmd_out->buzzer = 1;
        snprintf(msg, sizeof(msg),
            "CRITICAL: Voltage reached %.2fV (Limit: %.2fV). Overcharge safety triggered. Battery isolated.",
            voltage, VOLTAGE_CRITICAL_HIGH);
        _create_alert_if_not_exists("Over-Voltage", "CRITICAL", msg);
        _create_system_log("HEALING_ACTION", msg);
    }
    /* ── Check 4: Under-Voltage ── */
    else if (voltage < VOLTAGE_CRITICAL_LOW) {
        critical_triggered = 1;
        cmd_out->relay = 0;
        snprintf(msg, sizeof(msg),
            "CRITICAL: Voltage dropped to %.2fV (Limit: %.2fV). Undercharge safety triggered. Battery isolated.",
            voltage, VOLTAGE_CRITICAL_LOW);
        _create_alert_if_not_exists("Under-Voltage", "CRITICAL", msg);
        _create_system_log("HEALING_ACTION", msg);
    }

    /* ── Check 5: Over-Current ── */
    if (current > CURRENT_CRITICAL_HIGH) {
        critical_triggered = 1;
        cmd_out->relay  = 0;
        cmd_out->buzzer = 1;
        snprintf(msg, sizeof(msg),
            "CRITICAL: Current surged to %.2fA (Limit: %.2fA). Discharging short safety triggered. Battery isolated.",
            current, CURRENT_CRITICAL_HIGH);
        _create_alert_if_not_exists("Over-Current", "CRITICAL", msg);
        _create_system_log("HEALING_ACTION", msg);
    }

    /* ── Check 6: Recovery ── */
    int in_safe_zone = (temp    <  TEMP_RECOVERY_C &&
                        voltage >  VOLTAGE_RECOVERY_LOW &&
                        voltage <  VOLTAGE_RECOVERY_HIGH &&
                        current <  CURRENT_RECOVERY);

    int unresolved_count = db_alert_count_unresolved();

    if (in_safe_zone && unresolved_count > 0) {
        /* Check if any unresolved alert is CRITICAL */
        Alert unresolved[32];
        int n = db_alert_list_unresolved(unresolved, 32);
        int has_critical = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(unresolved[i].severity, "CRITICAL") == 0) { has_critical = 1; break; }
        }

        int should_recover = 0;
        if (has_critical) {
            if (cmd_out->reset_alarm) {
                should_recover      = 1;
                cmd_out->reset_alarm = 0;
            }
        } else {
            should_recover = 1; /* Warnings resolve automatically */
        }

        if (should_recover) {
            db_alert_resolve_all();
            cmd_out->relay  = 1;
            cmd_out->fan    = 0;
            cmd_out->buzzer = 0;
            _create_system_log("RECOVERY",
                "RECOVERY: All parameters in nominal operating zones. "
                "Isolation relay re-connected, indicators cleared.");
        }
    }

    /* Clear reset_alarm if no alerts remain */
    if (db_alert_count_unresolved() == 0 && cmd_out->reset_alarm) {
        cmd_out->reset_alarm = 0;
    }

    /* ── Apply user mode overrides (only when no critical fault active) ── */
    if (!critical_triggered) {
        if (strcmp(cmd_out->relay_mode, "on")  == 0) cmd_out->relay = 1;
        else if (strcmp(cmd_out->relay_mode, "off") == 0) cmd_out->relay = 0;

        if (strcmp(cmd_out->fan_mode, "on")  == 0) cmd_out->fan = 1;
        else if (strcmp(cmd_out->fan_mode, "off") == 0) cmd_out->fan = 0;
    } else {
        /* Under active critical danger, safety override isolates load */
        cmd_out->relay = 0;
    }

    db_command_update(cmd_out);
    return 1;
}
