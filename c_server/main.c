/*
 * main.c — Entry point for the Self-Healing EV Battery Simulator C server.
 */
#include "database.h"
#include "http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("==========================================================\n");
    printf("  Self-Healing EV Battery Simulator - Digital Twin (C)\n");
    printf("==========================================================\n");

    /* Ensure database directory exists */
    MKDIR("database");

    /* Open SQLite database */
    if (!db_open("database/battery_twin.db")) {
        fprintf(stderr, "FATAL: Cannot open database\n");
        return 1;
    }
    printf("[DB] Connected: database/battery_twin.db\n");

    /* Initialize tables */
    if (!db_init_tables()) {
        fprintf(stderr, "FATAL: Cannot initialize tables\n");
        db_close();
        return 1;
    }
    printf("[DB] Tables initialized\n");

    /* Seed default data */
    db_seed();
    printf("[DB] Seed complete\n");

    /* Cleanup any expired sessions on startup */
    db_session_cleanup_expired();

    /* Start HTTP server */
    printf("[SRV] Starting HTTP server on port 5000...\n");
    printf("[SRV] Open: http://127.0.0.1:5000\n");
    printf("[SRV] Login: admin@battery.twin / AdminPassword123\n");
    printf("----------------------------------------------------------\n");
    fflush(stdout);

    if (!http_server_run(5000)) {
        fprintf(stderr, "FATAL: HTTP server failed to start\n");
        db_close();
        return 1;
    }

    db_close();
    return 0;
}
