#ifdef _WIN32
#include <winsock2.h>
#endif
#include "http/http_server.h"
#include "config/env_loader.h"
#include "database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile int g_keep_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_keep_running = 0;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    const char *env_path = argc > 1 ? argv[1] : ".env";
    load_env_file(env_path);

    const char *env_db_host = getenv("DB_HOST");
    const char *env_db_port = getenv("DB_PORT");
    const char *env_db_name = getenv("DB_NAME");
    const char *env_db_user = getenv("DB_USER");
    const char *env_db_pass = getenv("DB_PASSWORD");
    const char *env_db_ssl = getenv("DB_SSL_MODE");
    const char *env_app_port = getenv("APP_PORT");
    const char *env_app_env = getenv("APP_ENV");
    const char *env_allowed_origins = getenv("ALLOWED_ORIGIN");

    const char *host = env_db_host ? env_db_host : "127.0.0.1";
    int db_port = env_db_port ? atoi(env_db_port) : 3307;
    const char *dbname = env_db_name ? env_db_name : "settlesmart_test";
    const char *user = env_db_user ? env_db_user : "settlesmart_user";
    const char *pass = env_db_pass ? env_db_pass : "dev_password_123";
    const char *ssl = env_db_ssl ? env_db_ssl : "preferred";
    int app_port = env_app_port ? atoi(env_app_port) : 8000;
    const char *app_env = env_app_env ? env_app_env : "development";
    const char *origins = env_allowed_origins ? env_allowed_origins : "http://localhost:3000,http://127.0.0.1:3000,http://localhost:5173";

    DBClient db;
    if (db_client_init(&db) != 0) {
        fprintf(stderr, "Failed to initialize database client\n");
        return 1;
    }

    DBConfig db_cfg;
    memset(&db_cfg, 0, sizeof(db_cfg));
    db_cfg.host = host;
    db_cfg.port = db_port;
    db_cfg.database = dbname;
    db_cfg.user = user;
    db_cfg.password = pass;
    db_cfg.ssl_mode = ssl;

    if (db_client_connect(&db, &db_cfg) != 0) {
        fprintf(stderr, "Failed to connect to MySQL database at %s:%d/%s\n", host, db_port, dbname);
        return 1;
    }

    HttpServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.port = app_port;
    cfg.db_client = &db;
    strncpy(cfg.app_env, app_env, sizeof(cfg.app_env) - 1);
    strncpy(cfg.allowed_origins, origins, sizeof(cfg.allowed_origins) - 1);
    cfg.enable_public_registration = 1;
    cfg.cookie_secure = (strcmp(app_env, "production") == 0);
    snprintf(cfg.instance_id, sizeof(cfg.instance_id), "server_%d", app_port);

    if (http_server_start(&cfg) != 0) {
        fprintf(stderr, "Failed to start HTTP server on port %d\n", app_port);
        db_client_close(&db);
        return 1;
    }

    printf("SettleSmart 2.0 backend server listening on port %d\n", app_port);
    printf("Allowed origins: %s\n", cfg.allowed_origins);

    while (g_keep_running) {
        http_server_poll(50);
    }

    printf("\nShutting down SettleSmart 2.0 backend server...\n");
    http_server_stop();
    db_client_close(&db);
    printf("Server stopped cleanly\n");
    return 0;
}
