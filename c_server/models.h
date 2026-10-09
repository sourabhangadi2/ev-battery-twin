/*
 * models.h — C struct definitions mirroring Python SQLAlchemy models.
 * Used across database, twin_engine, healing_engine, and route handlers.
 */
#ifndef MODELS_H
#define MODELS_H

#include <stdint.h>
#include <time.h>

#define STR_SM   64
#define STR_MED  128
#define STR_LG   256
#define STR_XL   512
#define STR_TEXT 2048

/* ── User ───────────────────────────────────────────────────────────── */
typedef struct {
    int    id;
    char   name[STR_MED];
    char   email[STR_MED];
    char   password_hash[STR_LG];
    char   role[STR_SM];
    time_t created_at;
} User;

/* ── SensorData ─────────────────────────────────────────────────────── */
typedef struct {
    int    id;
    double temperature;
    double humidity;
    double voltage;
    double current;
    double power;
    double battery_percentage;
    time_t timestamp;
} SensorData;

/* ── BatteryHealth ──────────────────────────────────────────────────── */
typedef struct {
    int    id;
    double health_score;        /* SOH % */
    double degradation;         /* Degradation % */
    int    estimated_cycles;
    int    estimated_remaining_life; /* RUL in cycles */
    time_t timestamp;
} BatteryHealth;

/* ── Alert ──────────────────────────────────────────────────────────── */
typedef struct {
    int    id;
    char   alert_type[STR_MED];
    char   severity[STR_SM];   /* "WARNING" or "CRITICAL" */
    char   message[STR_LG];
    time_t timestamp;
    int    resolved;           /* 0 = false, 1 = true */
} Alert;

/* ── Command ────────────────────────────────────────────────────────── */
typedef struct {
    int    id;
    int    relay;              /* 1 = connected, 0 = isolated */
    int    fan;               /* 1 = on, 0 = off */
    int    buzzer;            /* 1 = on, 0 = off */
    int    reset_alarm;       /* 1 = reset requested */
    char   fan_mode[STR_SM];  /* "auto", "on", "off" */
    char   relay_mode[STR_SM];
    time_t timestamp;
} Command;

/* ── SystemLog ──────────────────────────────────────────────────────── */
typedef struct {
    int    id;
    char   event[STR_MED];
    char   description[STR_TEXT];
    time_t timestamp;
} SystemLog;

/* ── Session (auth) ─────────────────────────────────────────────────── */
typedef struct {
    int    id;
    int    user_id;
    char   token[STR_LG];
    time_t expires_at;
} Session;

#endif /* MODELS_H */
