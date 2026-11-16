#include "db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int db_client_init(DBClient *client) {
    if (!client) {
        return -1;
    }
    memset(client, 0, sizeof(DBClient));
    client->connection = mysql_init(NULL);
    if (!client->connection) {
        snprintf(client->last_error, sizeof(client->last_error), "Failed to initialize MySQL structure");
        return -1;
    }
    return 0;
}

int db_client_connect(DBClient *client, const DBConfig *config) {
    if (!client || !config) {
        return -1;
    }
    if (!client->connection) {
        if (db_client_init(client) != 0) {
            return -1;
        }
    }

    if (config->ssl_mode) {
        unsigned int ssl_mode_val = SSL_MODE_PREFERRED;
        if (strcmp(config->ssl_mode, "disabled") == 0 || strcmp(config->ssl_mode, "DISABLED") == 0) {
            ssl_mode_val = SSL_MODE_DISABLED;
        } else if (strcmp(config->ssl_mode, "required") == 0 || strcmp(config->ssl_mode, "REQUIRED") == 0) {
            ssl_mode_val = SSL_MODE_REQUIRED;
        } else if (strcmp(config->ssl_mode, "verify_ca") == 0 || strcmp(config->ssl_mode, "VERIFY_CA") == 0) {
            ssl_mode_val = SSL_MODE_VERIFY_CA;
        } else if (strcmp(config->ssl_mode, "verify_identity") == 0 || strcmp(config->ssl_mode, "VERIFY_IDENTITY") == 0) {
            ssl_mode_val = SSL_MODE_VERIFY_IDENTITY;
        }
        mysql_options(client->connection, MYSQL_OPT_SSL_MODE, &ssl_mode_val);
    }

    if (!mysql_real_connect(
            client->connection,
            config->host,
            config->user,
            config->password,
            config->database,
            config->port,
            NULL,
            0)) {
        snprintf(client->last_error, sizeof(client->last_error), "%s", mysql_error(client->connection));
        return -1;
    }

    if (mysql_query(client->connection, "SET time_zone = '+00:00'") != 0) {
        snprintf(client->last_error, sizeof(client->last_error), "%s", mysql_error(client->connection));
        return -1;
    }

    return 0;
}

void db_client_close(DBClient *client) {
    if (client && client->connection) {
        mysql_close(client->connection);
        client->connection = NULL;
    }
}

int db_client_ping(DBClient *client) {
    if (!client || !client->connection) {
        return -1;
    }
    return mysql_ping(client->connection);
}

int db_client_query(DBClient *client, const char *sql) {
    if (!client || !client->connection || !sql) {
        return -1;
    }
    if (mysql_query(client->connection, sql) != 0) {
        snprintf(client->last_error, sizeof(client->last_error), "%s", mysql_error(client->connection));
        return -1;
    }
    return 0;
}

MYSQL_RES *db_client_store_result(DBClient *client) {
    if (!client || !client->connection) {
        return NULL;
    }
    return mysql_store_result(client->connection);
}

void db_client_free_result(MYSQL_RES *result) {
    if (result) {
        mysql_free_result(result);
    }
}

int db_client_begin(DBClient *client) {
    return db_client_query(client, "START TRANSACTION");
}

int db_client_commit(DBClient *client) {
    return db_client_query(client, "COMMIT");
}

int db_client_rollback(DBClient *client) {
    return db_client_query(client, "ROLLBACK");
}

uint64_t db_client_last_insert_id(DBClient *client) {
    if (!client || !client->connection) {
        return 0;
    }
    return mysql_insert_id(client->connection);
}

const char *db_client_get_error(const DBClient *client) {
    if (!client) {
        return "Unknown error";
    }
    return client->last_error;
}

MYSQL_STMT *db_client_prepare(DBClient *client, const char *sql) {
    MYSQL_STMT *stmt;
    if (!client || !client->connection || !sql) {
        return NULL;
    }
    stmt = mysql_stmt_init(client->connection);
    if (!stmt) {
        snprintf(client->last_error, sizeof(client->last_error), "Failed to initialize statement");
        return NULL;
    }
    if (mysql_stmt_prepare(stmt, sql, (unsigned long)strlen(sql)) != 0) {
        snprintf(client->last_error, sizeof(client->last_error), "%s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return NULL;
    }
    return stmt;
}

int db_client_execute_stmt(DBClient *client, MYSQL_STMT *stmt, MYSQL_BIND *params) {
    if (!client || !stmt) {
        return -1;
    }
    if (params) {
        if (mysql_stmt_bind_param(stmt, params) != 0) {
            snprintf(client->last_error, sizeof(client->last_error), "%s", mysql_stmt_error(stmt));
            return -1;
        }
    }
    if (mysql_stmt_execute(stmt) != 0) {
        snprintf(client->last_error, sizeof(client->last_error), "%s", mysql_stmt_error(stmt));
        return -1;
    }
    return 0;
}

void db_client_close_stmt(MYSQL_STMT *stmt) {
    if (stmt) {
        mysql_stmt_close(stmt);
    }
}
