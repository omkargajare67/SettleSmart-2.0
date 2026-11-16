#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(int argc, char **argv) {
    const char *env_file = ".env";
    DBConfig config;
    DBClient client;
    MYSQL_RES *res;
    MYSQL_ROW row;
    uint64_t id1;
    uint64_t id2;
    uint64_t id3;
    MYSQL_STMT *stmt;
    MYSQL_BIND bind_params[2];
    char title_param[100];
    char amount_param[32];
    unsigned long title_len;
    unsigned long amount_len;

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

    if (db_client_init(&client) != 0) {
        printf("FAILED: db_client_init: %s\n", db_client_get_error(&client));
        return 1;
    }

    if (db_client_connect(&client, &config) != 0) {
        printf("FAILED: db_client_connect: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }

    if (db_client_query(&client, "DROP TABLE IF EXISTS test_financial_records;") != 0) {
        printf("FAILED: Clean table drop: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }

    if (db_client_query(&client,
            "CREATE TABLE test_financial_records ("
            "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            "  title VARCHAR(100) NOT NULL,"
            "  amount DECIMAL(15, 2) NOT NULL,"
            "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB;") != 0) {
        printf("FAILED: Create table: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Created isolated test table test_financial_records\n");

    stmt = db_client_prepare(&client, "INSERT INTO test_financial_records (title, amount) VALUES (?, ?);");
    if (!stmt) {
        printf("FAILED: Prepare statement: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }

    memset(bind_params, 0, sizeof(bind_params));
    strncpy(title_param, "Initial Transfer A to B", sizeof(title_param));
    title_len = (unsigned long)strlen(title_param);
    strncpy(amount_param, "500.25", sizeof(amount_param));
    amount_len = (unsigned long)strlen(amount_param);

    bind_params[0].buffer_type = MYSQL_TYPE_STRING;
    bind_params[0].buffer = title_param;
    bind_params[0].buffer_length = sizeof(title_param);
    bind_params[0].length = &title_len;

    bind_params[1].buffer_type = MYSQL_TYPE_DECIMAL;
    bind_params[1].buffer = amount_param;
    bind_params[1].buffer_length = sizeof(amount_param);
    bind_params[1].length = &amount_len;

    if (db_client_execute_stmt(&client, stmt, bind_params) != 0) {
        printf("FAILED: Execute prepared insert: %s\n", db_client_get_error(&client));
        db_client_close_stmt(stmt);
        db_client_close(&client);
        return 1;
    }
    db_client_close_stmt(stmt);

    id1 = db_client_last_insert_id(&client);
    if (id1 == 0) {
        printf("FAILED: AUTO_INCREMENT ID was 0\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Prepared insert executed and generated AUTO_INCREMENT ID: %llu\n", (unsigned long long)id1);

    if (db_client_query(&client, "SELECT id, title, amount FROM test_financial_records WHERE id = 1;") != 0) {
        printf("FAILED: Query record: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    if (!row) {
        printf("FAILED: No row returned for ID 1\n");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    if (strcmp(row[1], "Initial Transfer A to B") != 0) {
        printf("FAILED: Title mismatch: got %s\n", row[1]);
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    if (strcmp(row[2], "500.25") != 0) {
        printf("FAILED: Amount mismatch: expected 500.25, got %s\n", row[2]);
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Retrieved record exactly matches title and exact DECIMAL amount: %s\n", row[2]);
    db_client_free_result(res);

    if (db_client_query(&client, "UPDATE test_financial_records SET amount = 750.50 WHERE id = 1;") != 0) {
        printf("FAILED: Update query: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    if (db_client_query(&client, "SELECT amount FROM test_financial_records WHERE id = 1;") != 0) {
        printf("FAILED: Query updated record: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    if (!row || strcmp(row[0], "750.50") != 0) {
        printf("FAILED: Updated amount mismatch: got %s\n", row ? row[0] : "NULL");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Record updated to new DECIMAL value: %s\n", row[0]);
    db_client_free_result(res);

    if (db_client_begin(&client) != 0) {
        printf("FAILED: Begin transaction: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    if (db_client_query(&client, "INSERT INTO test_financial_records (title, amount) VALUES ('Commit Record', 1000.00);") != 0) {
        printf("FAILED: Insert inside transaction: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    id2 = db_client_last_insert_id(&client);
    if (db_client_commit(&client) != 0) {
        printf("FAILED: Commit transaction: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Transaction committed record with ID: %llu\n", (unsigned long long)id2);

    if (db_client_query(&client, "SELECT COUNT(*) FROM test_financial_records WHERE id = 2;") != 0) {
        printf("FAILED: Verify committed record: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    if (!row || strcmp(row[0], "1") != 0) {
        printf("FAILED: Committed record count is not 1\n");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    db_client_free_result(res);
    printf("PASSED: Verified committed transaction record persisted\n");

    if (db_client_begin(&client) != 0) {
        printf("FAILED: Begin second transaction: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    if (db_client_query(&client, "INSERT INTO test_financial_records (title, amount) VALUES ('Rollback Record', 9999.99);") != 0) {
        printf("FAILED: Insert rollback candidate: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    id3 = db_client_last_insert_id(&client);
    if (db_client_rollback(&client) != 0) {
        printf("FAILED: Rollback transaction: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Rolled back transaction with uncommitted ID: %llu\n", (unsigned long long)id3);

    if (db_client_query(&client, "SELECT COUNT(*) FROM test_financial_records WHERE title = 'Rollback Record';") != 0) {
        printf("FAILED: Query rolled back record: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    if (!row || strcmp(row[0], "0") != 0) {
        printf("FAILED: Rolled back record should not exist, count is %s\n", row ? row[0] : "NULL");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    db_client_free_result(res);
    printf("PASSED: Verified rolled back record does NOT exist in table\n");

    if (db_client_query(&client, "DELETE FROM test_financial_records WHERE id = 1;") != 0) {
        printf("FAILED: Delete record: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    if (db_client_query(&client, "SELECT COUNT(*) FROM test_financial_records WHERE id = 1;") != 0) {
        printf("FAILED: Verify deleted record: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    if (!row || strcmp(row[0], "0") != 0) {
        printf("FAILED: Deleted record still exists\n");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    db_client_free_result(res);
    printf("PASSED: Deleted record verified removed\n");

    if (db_client_query(&client, "DROP TABLE test_financial_records;") != 0) {
        printf("FAILED: Cleanup drop table: %s\n", db_client_get_error(&client));
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Cleaned up test_financial_records table\n");

    db_client_close(&client);
    printf("\nALL DUMMY-TABLE TESTS SUCCEEDED.\n");
    return 0;
}
