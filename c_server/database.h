/*
 * database.h — SQLite CRUD helpers and database initialization.
 */
#ifndef DATABASE_H
#define DATABASE_H

#include "sqlite3.h"
#include "models.h"

/* Global DB handle — opened once in main.c */
extern sqlite3 *g_db;

/* ── Lifecycle ──────────────────────────────────────────────────────── */
int  db_open(const char *path);
void db_close(void);
int  db_init_tables(void);
int  db_seed(void);

/* ── User ───────────────────────────────────────────────────────────── */
int  db_user_find_by_email(const char *email, User *out);
int  db_user_find_by_id(int id, User *out);
int  db_user_insert(const User *u);

/* ── SensorData ─────────────────────────────────────────────────────── */
int  db_sensor_insert(SensorData *s);       /* fills s->id on success */
int  db_sensor_latest(SensorData *out);
int  db_sensor_prev(int current_id, SensorData *out);
/* Returns count of records; records must point to array of at least max_count */
int  db_sensor_history(SensorData *records, int max_count);
/* Update battery_percentage for a just-inserted sensor row */
int  db_sensor_update_soc(int id, double soc);

/* ── BatteryHealth ──────────────────────────────────────────────────── */
int  db_health_insert(const BatteryHealth *h);
int  db_health_latest(BatteryHealth *out);

/* ── Alert ──────────────────────────────────────────────────────────── */
int  db_alert_insert(const Alert *a);
/* Find first unresolved alert matching type+severity; returns 1 if found */
int  db_alert_find_unresolved(const char *type, const char *severity, Alert *out);
/* Get all unresolved alerts; returns count (up to max) */
int  db_alert_list_unresolved(Alert *arr, int max);
/* Get all unresolved alerts count */
int  db_alert_count_unresolved(void);
int  db_alert_resolve_all(void);
/* Get recent alerts (resolved or not) for dashboard */
int  db_alert_list_recent(Alert *arr, int max);

/* ── Command ────────────────────────────────────────────────────────── */
int  db_command_get_or_create(Command *out);
int  db_command_update(const Command *c);

/* ── SystemLog ──────────────────────────────────────────────────────── */
int  db_log_insert(const char *event, const char *desc);
/* Returns last log row; 0 if none */
int  db_log_latest(SystemLog *out);
/* Get recent N logs */
int  db_log_list_recent(SystemLog *arr, int max);

/* ── Session ────────────────────────────────────────────────────────── */
int  db_session_create(int user_id, const char *token, time_t expires_at);
int  db_session_find(const char *token, Session *out);
int  db_session_delete(const char *token);
void db_session_cleanup_expired(void);

#endif /* DATABASE_H */
