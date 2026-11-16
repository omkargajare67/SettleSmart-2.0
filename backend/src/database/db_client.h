#ifndef DB_CLIENT_H
#define DB_CLIENT_H

#include <mysql.h>
#include <stdint.h>

typedef struct {
    MYSQL *connection;
    char last_error[512];
} DBClient;

typedef struct {
    const char *host;
    int port;
    const char *database;
    const char *user;
    const char *password;
    const char *ssl_mode;
} DBConfig;

int db_client_init(DBClient *client);
int db_client_connect(DBClient *client, const DBConfig *config);
void db_client_close(DBClient *client);
int db_client_ping(DBClient *client);

int db_client_query(DBClient *client, const char *sql);
MYSQL_RES *db_client_store_result(DBClient *client);
void db_client_free_result(MYSQL_RES *result);

int db_client_begin(DBClient *client);
int db_client_commit(DBClient *client);
int db_client_rollback(DBClient *client);

uint64_t db_client_last_insert_id(DBClient *client);
const char *db_client_get_error(const DBClient *client);

MYSQL_STMT *db_client_prepare(DBClient *client, const char *sql);
int db_client_execute_stmt(DBClient *client, MYSQL_STMT *stmt, MYSQL_BIND *params);
void db_client_close_stmt(MYSQL_STMT *stmt);

#endif
