/*
 * database.c — SQLite CRUD implementation.
 * Mirrors app/database.py + app/models.py SQLAlchemy ORM logic.
 */
#include "database.h"
#include "auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

sqlite3 *g_db = NULL;

/* ── Lifecycle ──────────────────────────────────────────────────────── */

int db_open(const char *path) {
    int rc = sqlite3_open(path, &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(g_db));
        return 0;
    }
    /* Enable WAL mode and foreign keys */
    sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(g_db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    return 1;
}

void db_close(void) {
    if (g_db) { sqlite3_close(g_db); g_db = NULL; }
}

int db_init_tables(void) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  email TEXT UNIQUE NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  role TEXT DEFAULT 'operator',"
        "  created_at INTEGER DEFAULT (strftime('%s','now'))"
        ");"
        "CREATE TABLE IF NOT EXISTS sensor_data ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  temperature REAL NOT NULL,"
        "  humidity REAL NOT NULL,"
        "  voltage REAL NOT NULL,"
        "  current REAL NOT NULL,"
        "  power REAL NOT NULL,"
        "  battery_percentage REAL NOT NULL,"
        "  timestamp INTEGER DEFAULT (strftime('%s','now'))"
        ");"
        "CREATE TABLE IF NOT EXISTS battery_health ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  health_score REAL NOT NULL,"
        "  degradation REAL NOT NULL,"
        "  estimated_cycles INTEGER NOT NULL,"
        "  estimated_remaining_life INTEGER NOT NULL,"
        "  timestamp INTEGER DEFAULT (strftime('%s','now'))"
        ");"
        "CREATE TABLE IF NOT EXISTS alerts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  alert_type TEXT NOT NULL,"
        "  severity TEXT NOT NULL,"
        "  message TEXT NOT NULL,"
        "  timestamp INTEGER DEFAULT (strftime('%s','now')),"
        "  resolved INTEGER DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS commands ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  relay INTEGER DEFAULT 1,"
        "  fan INTEGER DEFAULT 0,"
        "  buzzer INTEGER DEFAULT 0,"
        "  reset_alarm INTEGER DEFAULT 0,"
        "  fan_mode TEXT DEFAULT 'auto',"
        "  relay_mode TEXT DEFAULT 'auto',"
        "  timestamp INTEGER DEFAULT (strftime('%s','now'))"
        ");"
        "CREATE TABLE IF NOT EXISTS system_logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  event TEXT NOT NULL,"
        "  description TEXT NOT NULL,"
        "  timestamp INTEGER DEFAULT (strftime('%s','now'))"
        ");"
        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  token TEXT UNIQUE NOT NULL,"
        "  expires_at INTEGER NOT NULL"
        ");";

    char *errmsg = NULL;
    int rc = sqlite3_exec(g_db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "db_init_tables: %s\n", errmsg);
        sqlite3_free(errmsg);
        return 0;
    }
    return 1;
}

int db_seed(void) {
    /* ── Seed default admin user ────────────────────────────────────── */
    User admin;
    if (!db_user_find_by_email("admin@battery.twin", &admin)) {
        memset(&admin, 0, sizeof(admin));
        strncpy(admin.name,  "System Administrator", sizeof(admin.name)-1);
        strncpy(admin.email, "admin@battery.twin",   sizeof(admin.email)-1);
        strncpy(admin.role,  "admin",                sizeof(admin.role)-1);
        auth_hash_password("AdminPassword123", admin.password_hash, sizeof(admin.password_hash));
        db_user_insert(&admin);
        db_log_insert("SYSTEM_INIT", "Database initialized. Default administrator account seeded successfully.");
    }

    /* ── Seed default Command row ───────────────────────────────────── */
    Command cmd;
    if (!db_command_get_or_create(&cmd)) {
        /* Already handled inside get_or_create */
    }

    /* ── Seed initial SensorData + BatteryHealth ────────────────────── */
    SensorData s;
    if (!db_sensor_latest(&s)) {
        SensorData init = {0};
        init.temperature       = 26.5;
        init.humidity          = 52.0;
        init.voltage           = 3.85;
        init.current           = 1.20;
        init.power             = 4.62;
        init.battery_percentage = 70.8;
        init.timestamp         = time(NULL);
        db_sensor_insert(&init);

        BatteryHealth h = {0};
        h.health_score            = 98.5;
        h.degradation             = 1.5;
        h.estimated_cycles        = 12;
        h.estimated_remaining_life = 950;
        h.timestamp               = time(NULL);
        db_health_insert(&h);
    }
    return 1;
}

/* ── User ───────────────────────────────────────────────────────────── */

int db_user_find_by_email(const char *email, User *out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,name,email,password_hash,role,created_at FROM users WHERE email=? LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, email, -1, SQLITE_STATIC);
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id = sqlite3_column_int(stmt, 0);
        strncpy(out->name,          (const char*)sqlite3_column_text(stmt,1), sizeof(out->name)-1);
        strncpy(out->email,         (const char*)sqlite3_column_text(stmt,2), sizeof(out->email)-1);
        strncpy(out->password_hash, (const char*)sqlite3_column_text(stmt,3), sizeof(out->password_hash)-1);
        strncpy(out->role,          (const char*)sqlite3_column_text(stmt,4), sizeof(out->role)-1);
        out->created_at = (time_t)sqlite3_column_int64(stmt,5);
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

int db_user_find_by_id(int id, User *out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,name,email,password_hash,role,created_at FROM users WHERE id=? LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, id);
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id = sqlite3_column_int(stmt, 0);
        strncpy(out->name,          (const char*)sqlite3_column_text(stmt,1), sizeof(out->name)-1);
        strncpy(out->email,         (const char*)sqlite3_column_text(stmt,2), sizeof(out->email)-1);
        strncpy(out->password_hash, (const char*)sqlite3_column_text(stmt,3), sizeof(out->password_hash)-1);
        strncpy(out->role,          (const char*)sqlite3_column_text(stmt,4), sizeof(out->role)-1);
        out->created_at = (time_t)sqlite3_column_int64(stmt,5);
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

int db_user_insert(const User *u) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users(name,email,password_hash,role,created_at) VALUES(?,?,?,?,?);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, u->name,          -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, u->email,         -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, u->password_hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, u->role,          -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt,5, (sqlite3_int64)(u->created_at ? u->created_at : time(NULL)));
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

/* ── SensorData ─────────────────────────────────────────────────────── */

int db_sensor_insert(SensorData *s) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO sensor_data(temperature,humidity,voltage,current,power,battery_percentage,timestamp)"
        " VALUES(?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_double(stmt,1, s->temperature);
    sqlite3_bind_double(stmt,2, s->humidity);
    sqlite3_bind_double(stmt,3, s->voltage);
    sqlite3_bind_double(stmt,4, s->current);
    sqlite3_bind_double(stmt,5, s->power);
    sqlite3_bind_double(stmt,6, s->battery_percentage);
    sqlite3_bind_int64(stmt,7, (sqlite3_int64)(s->timestamp ? s->timestamp : time(NULL)));
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    if (ok) s->id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    return ok;
}

static void _sensor_from_stmt(sqlite3_stmt *stmt, SensorData *out) {
    out->id                 = sqlite3_column_int(stmt, 0);
    out->temperature        = sqlite3_column_double(stmt, 1);
    out->humidity           = sqlite3_column_double(stmt, 2);
    out->voltage            = sqlite3_column_double(stmt, 3);
    out->current            = sqlite3_column_double(stmt, 4);
    out->power              = sqlite3_column_double(stmt, 5);
    out->battery_percentage = sqlite3_column_double(stmt, 6);
    out->timestamp          = (time_t)sqlite3_column_int64(stmt, 7);
}

int db_sensor_latest(SensorData *out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,temperature,humidity,voltage,current,power,battery_percentage,timestamp"
                      " FROM sensor_data ORDER BY id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) { _sensor_from_stmt(stmt, out); found = 1; }
    sqlite3_finalize(stmt);
    return found;
}

int db_sensor_prev(int current_id, SensorData *out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,temperature,humidity,voltage,current,power,battery_percentage,timestamp"
                      " FROM sensor_data WHERE id<? ORDER BY id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, current_id);
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) { _sensor_from_stmt(stmt, out); found = 1; }
    sqlite3_finalize(stmt);
    return found;
}

int db_sensor_history(SensorData *records, int max_count) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,temperature,humidity,voltage,current,power,battery_percentage,timestamp"
                      " FROM sensor_data ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, max_count);
    int count = 0;
    /* Collect in reverse order, then reverse array */
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        _sensor_from_stmt(stmt, &records[count]);
        count++;
    }
    sqlite3_finalize(stmt);
    /* Reverse to get chronological order */
    for (int i=0,k=count-1; i<k; i++,k--) {
        SensorData tmp = records[i]; records[i] = records[k]; records[k] = tmp;
    }
    return count;
}

int db_sensor_update_soc(int id, double soc) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE sensor_data SET battery_percentage=? WHERE id=?;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_double(stmt, 1, soc);
    sqlite3_bind_int(stmt, 2, id);
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

/* ── BatteryHealth ──────────────────────────────────────────────────── */

int db_health_insert(const BatteryHealth *h) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO battery_health(health_score,degradation,estimated_cycles,estimated_remaining_life,timestamp)"
        " VALUES(?,?,?,?,?);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_double(stmt,1, h->health_score);
    sqlite3_bind_double(stmt,2, h->degradation);
    sqlite3_bind_int(stmt,3,   h->estimated_cycles);
    sqlite3_bind_int(stmt,4,   h->estimated_remaining_life);
    sqlite3_bind_int64(stmt,5, (sqlite3_int64)(h->timestamp ? h->timestamp : time(NULL)));
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int db_health_latest(BatteryHealth *out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,health_score,degradation,estimated_cycles,estimated_remaining_life,timestamp"
                      " FROM battery_health ORDER BY id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id                       = sqlite3_column_int(stmt,0);
        out->health_score             = sqlite3_column_double(stmt,1);
        out->degradation              = sqlite3_column_double(stmt,2);
        out->estimated_cycles         = sqlite3_column_int(stmt,3);
        out->estimated_remaining_life = sqlite3_column_int(stmt,4);
        out->timestamp                = (time_t)sqlite3_column_int64(stmt,5);
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

/* ── Alert ──────────────────────────────────────────────────────────── */

int db_alert_insert(const Alert *a) {
    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT INTO alerts(alert_type,severity,message,timestamp,resolved)"
        " VALUES(?,?,?,?,0);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt,1, a->alert_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,2, a->severity,   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,3, a->message,    -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt,4,(sqlite3_int64)time(NULL));
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int db_alert_find_unresolved(const char *type, const char *severity, Alert *out) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id,alert_type,severity,message,timestamp,resolved FROM alerts"
        " WHERE alert_type=? AND severity=? AND resolved=0 LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt,1, type,     -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,2, severity, -1, SQLITE_STATIC);
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id = sqlite3_column_int(stmt,0);
        strncpy(out->alert_type, (const char*)sqlite3_column_text(stmt,1), sizeof(out->alert_type)-1);
        strncpy(out->severity,   (const char*)sqlite3_column_text(stmt,2), sizeof(out->severity)-1);
        strncpy(out->message,    (const char*)sqlite3_column_text(stmt,3), sizeof(out->message)-1);
        out->timestamp = (time_t)sqlite3_column_int64(stmt,4);
        out->resolved  = sqlite3_column_int(stmt,5);
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

int db_alert_list_unresolved(Alert *arr, int max) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id,alert_type,severity,message,timestamp,resolved FROM alerts"
        " WHERE resolved=0 ORDER BY id DESC;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
        Alert *a = &arr[count];
        a->id = sqlite3_column_int(stmt,0);
        strncpy(a->alert_type, (const char*)sqlite3_column_text(stmt,1), sizeof(a->alert_type)-1);
        strncpy(a->severity,   (const char*)sqlite3_column_text(stmt,2), sizeof(a->severity)-1);
        strncpy(a->message,    (const char*)sqlite3_column_text(stmt,3), sizeof(a->message)-1);
        a->timestamp = (time_t)sqlite3_column_int64(stmt,4);
        a->resolved  = sqlite3_column_int(stmt,5);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

int db_alert_count_unresolved(void) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, "SELECT COUNT(*) FROM alerts WHERE resolved=0;", -1, &stmt, NULL) != SQLITE_OK) return 0;
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return n;
}

int db_alert_resolve_all(void) {
    char *err;
    int rc = sqlite3_exec(g_db, "UPDATE alerts SET resolved=1 WHERE resolved=0;", NULL, NULL, &err);
    if (rc != SQLITE_OK) { sqlite3_free(err); return 0; }
    return 1;
}

int db_alert_list_recent(Alert *arr, int max) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id,alert_type,severity,message,timestamp,resolved FROM alerts"
        " ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, max);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
        Alert *a = &arr[count];
        a->id = sqlite3_column_int(stmt,0);
        strncpy(a->alert_type, (const char*)sqlite3_column_text(stmt,1), sizeof(a->alert_type)-1);
        strncpy(a->severity,   (const char*)sqlite3_column_text(stmt,2), sizeof(a->severity)-1);
        strncpy(a->message,    (const char*)sqlite3_column_text(stmt,3), sizeof(a->message)-1);
        a->timestamp = (time_t)sqlite3_column_int64(stmt,4);
        a->resolved  = sqlite3_column_int(stmt,5);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ── Command ────────────────────────────────────────────────────────── */

int db_command_get_or_create(Command *out) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT id,relay,fan,buzzer,reset_alarm,fan_mode,relay_mode,timestamp"
        " FROM commands LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id          = sqlite3_column_int(stmt,0);
        out->relay       = sqlite3_column_int(stmt,1);
        out->fan         = sqlite3_column_int(stmt,2);
        out->buzzer      = sqlite3_column_int(stmt,3);
        out->reset_alarm = sqlite3_column_int(stmt,4);
        strncpy(out->fan_mode,   (const char*)sqlite3_column_text(stmt,5), sizeof(out->fan_mode)-1);
        strncpy(out->relay_mode, (const char*)sqlite3_column_text(stmt,6), sizeof(out->relay_mode)-1);
        out->timestamp   = (time_t)sqlite3_column_int64(stmt,7);
        found = 1;
    }
    sqlite3_finalize(stmt);
    if (found) return 1;

    /* Create default command row */
    memset(out, 0, sizeof(*out));
    out->relay = 1;
    strncpy(out->fan_mode,   "auto", sizeof(out->fan_mode)-1);
    strncpy(out->relay_mode, "auto", sizeof(out->relay_mode)-1);
    out->timestamp = time(NULL);

    const char *isql =
        "INSERT INTO commands(relay,fan,buzzer,reset_alarm,fan_mode,relay_mode,timestamp)"
        " VALUES(1,0,0,0,'auto','auto',?);";
    if (sqlite3_prepare_v2(g_db, isql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int64(stmt,1,(sqlite3_int64)out->timestamp);
    sqlite3_step(stmt);
    out->id = (int)sqlite3_last_insert_rowid(g_db);
    sqlite3_finalize(stmt);
    db_log_insert("SYSTEM_INIT","Default actuator commands set. Relay closed (ON), Fan (OFF), Buzzer (OFF).");
    return 1;
}

int db_command_update(const Command *c) {
    sqlite3_stmt *stmt;
    const char *sql =
        "UPDATE commands SET relay=?,fan=?,buzzer=?,reset_alarm=?,fan_mode=?,relay_mode=?,timestamp=?"
        " WHERE id=?;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt,1,   c->relay);
    sqlite3_bind_int(stmt,2,   c->fan);
    sqlite3_bind_int(stmt,3,   c->buzzer);
    sqlite3_bind_int(stmt,4,   c->reset_alarm);
    sqlite3_bind_text(stmt,5,  c->fan_mode,   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,6,  c->relay_mode, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt,7, (sqlite3_int64)time(NULL));
    sqlite3_bind_int(stmt,8,   c->id);
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

/* ── SystemLog ──────────────────────────────────────────────────────── */

int db_log_insert(const char *event, const char *desc) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO system_logs(event,description,timestamp) VALUES(?,?,?);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt,1, event, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,2, desc,  -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt,3,(sqlite3_int64)time(NULL));
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int db_log_latest(SystemLog *out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,event,description,timestamp FROM system_logs ORDER BY id DESC LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id = sqlite3_column_int(stmt,0);
        strncpy(out->event,       (const char*)sqlite3_column_text(stmt,1), sizeof(out->event)-1);
        strncpy(out->description, (const char*)sqlite3_column_text(stmt,2), sizeof(out->description)-1);
        out->timestamp = (time_t)sqlite3_column_int64(stmt,3);
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

int db_log_list_recent(SystemLog *arr, int max) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,event,description,timestamp FROM system_logs ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt,1,max);
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max) {
        SystemLog *l = &arr[count];
        l->id = sqlite3_column_int(stmt,0);
        strncpy(l->event,       (const char*)sqlite3_column_text(stmt,1), sizeof(l->event)-1);
        strncpy(l->description, (const char*)sqlite3_column_text(stmt,2), sizeof(l->description)-1);
        l->timestamp = (time_t)sqlite3_column_int64(stmt,3);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* ── Session ────────────────────────────────────────────────────────── */

int db_session_create(int user_id, const char *token, time_t expires_at) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO sessions(user_id,token,expires_at) VALUES(?,?,?);";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt,1,    user_id);
    sqlite3_bind_text(stmt,2,   token, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt,3,  (sqlite3_int64)expires_at);
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int db_session_find(const char *token, Session *out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id,user_id,token,expires_at FROM sessions WHERE token=? LIMIT 1;";
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt,1, token, -1, SQLITE_STATIC);
    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id         = sqlite3_column_int(stmt,0);
        out->user_id    = sqlite3_column_int(stmt,1);
        strncpy(out->token, (const char*)sqlite3_column_text(stmt,2), sizeof(out->token)-1);
        out->expires_at = (time_t)sqlite3_column_int64(stmt,3);
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

int db_session_delete(const char *token) {
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db,"DELETE FROM sessions WHERE token=?",-1,&stmt,NULL)!=SQLITE_OK) return 0;
    sqlite3_bind_text(stmt,1,token,-1,SQLITE_STATIC);
    int ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

void db_session_cleanup_expired(void) {
    char *err;
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM sessions WHERE expires_at < %lld;", (long long)time(NULL));
    sqlite3_exec(g_db, sql, NULL, NULL, &err);
    if (err) sqlite3_free(err);
}
