#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_successful_connection(const DBConfig *config) {
    DBClient client;
    MYSQL_RES *res;
    MYSQL_ROW row;

    if (db_client_init(&client) != 0) {
        printf("FAILED: db_client_init failed: %s\n", db_client_get_error(&client));
        return 1;
    }

    if (db_client_connect(&client, config) != 0) {
        printf("FAILED: db_client_connect failed: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Connected to MySQL database successfully\n");

    if (db_client_query(&client, "SELECT 1, VERSION();") != 0) {
        printf("FAILED: Query SELECT 1 failed: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }

    res = db_client_store_result(&client);
    if (!res) {
        printf("FAILED: Store result returned NULL\n");
        db_client_close(&client);
        return 1;
    }

    row = mysql_fetch_row(res);
    if (!row || strcmp(row[0], "1") != 0) {
        printf("FAILED: Expected '1', got unexpected value\n");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: SELECT 1 returned %s\n", row[0]);
    printf("INFO: MySQL Server Version: %s\n", row[1] ? row[1] : "Unknown");
    db_client_free_result(res);

    if (db_client_query(&client, "SHOW STATUS LIKE 'Ssl_cipher';") == 0) {
        res = db_client_store_result(&client);
        if (res) {
            row = mysql_fetch_row(res);
            if (row && row[1] && strlen(row[1]) > 0) {
                printf("INFO: Active TLS Cipher: %s\n", row[1]);
            } else {
                printf("INFO: Active TLS Cipher: None (Unencrypted)\n");
            }
            db_client_free_result(res);
        }
    }

    db_client_close(&client);
    printf("PASSED: Connection closed cleanly\n");
    return 0;
}

static int test_incorrect_password(const DBConfig *base_config) {
    DBClient client;
    DBConfig bad_config = *base_config;
    bad_config.password = "definitely_wrong_password_987654";

    if (db_client_init(&client) != 0) {
        return 1;
    }

    if (db_client_connect(&client, &bad_config) == 0) {
        printf("FAILED: Connection succeeded with invalid password\n");
        db_client_close(&client);
        return 1;
    }

    printf("PASSED: Incorrect password rejected as expected: %s\n", db_client_get_error(&client));
    db_client_close(&client);
    return 0;
}

static int test_incorrect_hostname(const DBConfig *base_config) {
    DBClient client;
    DBConfig bad_config = *base_config;
    bad_config.host = "invalid.host.nonexistent.local";

    if (db_client_init(&client) != 0) {
        return 1;
    }

    if (db_client_connect(&client, &bad_config) == 0) {
        printf("FAILED: Connection succeeded with invalid hostname\n");
        db_client_close(&client);
        return 1;
    }

    printf("PASSED: Incorrect hostname rejected as expected: %s\n", db_client_get_error(&client));
    db_client_close(&client);
    return 0;
}

static int test_unavailable_server(const DBConfig *base_config) {
    DBClient client;
    DBConfig bad_config = *base_config;
    bad_config.port = 64999;

    if (db_client_init(&client) != 0) {
        return 1;
    }

    if (db_client_connect(&client, &bad_config) == 0) {
        printf("FAILED: Connection succeeded on unavailable port\n");
        db_client_close(&client);
        return 1;
    }

    printf("PASSED: Unavailable port rejected as expected: %s\n", db_client_get_error(&client));
    db_client_close(&client);
    return 0;
}

int main(int argc, char **argv) {
    const char *env_file = ".env";
    DBConfig config;

    if (argc > 1) {
        env_file = argv[1];
    }

    load_env_file(env_file);

    config.host = get_env_value("DB_HOST", "127.0.0.1");
    config.port = get_env_int("DB_PORT", 3307);
    config.database = get_env_value("DB_NAME", "settlesmart_test");
    config.user = get_env_value("DB_USER", "settlesmart_user");
    config.password = get_env_value("DB_PASSWORD", "dev_password_123");
    config.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    printf("Running Connection Test...\n");
    if (test_successful_connection(&config) != 0) {
        printf("Connection test to target database failed. Check configuration and credentials.\n");
        return 1;
    }

    printf("\nRunning Error Handling Tests...\n");
    if (test_incorrect_password(&config) != 0) {
        return 1;
    }
    if (test_incorrect_hostname(&config) != 0) {
        return 1;
    }
    if (test_unavailable_server(&config) != 0) {
        return 1;
    }

    printf("\nALL DATABASE CONNECTION TESTS SUCCEEDED.\n");
    return 0;
}
