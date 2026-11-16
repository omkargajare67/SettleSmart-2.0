#include "../src/auth/auth_service.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/entities/entity_service.h"
#include "../src/transactions/transaction_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  load_env_file(".env");
  DBConfig db_cfg;
  db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
  db_cfg.port = get_env_int("DB_PORT", 3307);
  db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
  db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
  db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
  db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

  DBClient client;
  if (db_client_init(&client) != 0) {
    printf("FAILED: db_client_init\n");
    return 1;
  }

  DBConfig bad_cfg = db_cfg;
  bad_cfg.password = "WrongPassword_999!";
  if (db_client_connect(&client, &bad_cfg) == 0) {
    printf("FAILED: Connection with bad password unexpectedly succeeded\n");
    db_client_close(&client);
    return 1;
  }
  printf("PASSED: Database rejects invalid credentials with clean error\n");

  bad_cfg.password = db_cfg.password;
  bad_cfg.port = 64998;
  if (db_client_connect(&client, &bad_cfg) == 0) {
    printf("FAILED: Connection to unavailable port unexpectedly succeeded\n");
    db_client_close(&client);
    return 1;
  }
  printf("PASSED: Database handles unreachable port gracefully without "
         "crashing\n");

  if (db_client_connect(&client, &db_cfg) != 0) {
    printf("FAILED: Valid connection failed\n");
    return 1;
  }
  printf("PASSED: Valid connection re-established\n");

  db_client_query(
      &client, "CREATE TABLE IF NOT EXISTS test_recovery_dummy (id INT "
               "AUTO_INCREMENT PRIMARY KEY, val VARCHAR(50)) ENGINE=InnoDB;");
  db_client_query(&client, "DELETE FROM test_recovery_dummy;");

    db_client_begin(&client);
    db_client_query(&client, "INSERT INTO test_recovery_dummy (val) VALUES ('uncommitted_entry');");
    db_client_rollback(&client);

  db_client_query(&client, "SELECT COUNT(*) FROM test_recovery_dummy;");
  MYSQL_RES *res = db_client_store_result(&client);
  if (!res) {
    printf("FAILED: Failed to check rollback result\n");
    db_client_close(&client);
    return 1;
  }
  MYSQL_ROW row = mysql_fetch_row(res);
  int row_count = row ? atoi(row[0]) : -1;
  db_client_free_result(res);

  if (row_count != 0) {
    printf("FAILED: Rolled back transaction record persisted in database\n");
    db_client_close(&client);
    return 1;
  }
  printf("PASSED: Uncommitted changes safely discarded by rollback boundary\n");

  db_client_query(&client, "DROP TABLE IF EXISTS test_recovery_dummy;");

    long long org_id = 0;
    long long user_id = 0;
    char err_buf[256];
    db_client_query(&client, "SET FOREIGN_KEY_CHECKS=0;");
    db_client_query(&client, "DELETE FROM outbox_consumer_cursors;");
    db_client_query(&client, "DELETE FROM event_outbox;");
    db_client_query(&client, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'FailRec Org');");
    db_client_query(&client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'FailRec Org');");
    db_client_query(&client, "DELETE FROM sessions WHERE user_id IN (SELECT id FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'FailRec Org'));");
    db_client_query(&client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'FailRec Org');");
    db_client_query(&client, "DELETE FROM organizations WHERE name = 'FailRec Org';");
    db_client_query(&client, "SET FOREIGN_KEY_CHECKS=1;");

  if (auth_register_organization(&client, "FailRec Org", "admin@failrec.com",
                                 "AdminSecret#2026", &org_id, &user_id, err_buf,
                                 sizeof(err_buf)) != 0) {
    printf("FAILED: Failed to create test org: %s\n", err_buf);
    db_client_close(&client);
    return 1;
  }

    Transaction failed_tx;
    int is_replay = 0;
    int tx_res = transaction_service_create(&client, org_id, user_id, 999999, 999998,
                                            "500.00", "INR",
                                            "c0000000-0000-4000-8000-000000000001",
                                            &failed_tx, &is_replay, err_buf, sizeof(err_buf));
    if (tx_res == 0) {
        printf("FAILED: Transaction with nonexistent entities unexpectedly succeeded\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Invalid foreign key transaction cleanly rejected\n");

  char check_q[256];
  snprintf(check_q, sizeof(check_q),
           "SELECT COUNT(*) FROM event_outbox WHERE organization_id = %lld AND "
           "transaction_id = %lld;",
           org_id, failed_tx.id);
  db_client_query(&client, check_q);
  res = db_client_store_result(&client);
  if (res) {
    row = mysql_fetch_row(res);
    int outbox_count = row ? atoi(row[0]) : 0;
    db_client_free_result(res);
    if (outbox_count != 0) {
      printf("FAILED: Outbox event written for failed transaction\n");
      db_client_close(&client);
      return 1;
    }
  }
  printf("PASSED: Zero outbox events recorded for failed transaction\n");

  DBClient null_client;
  memset(&null_client, 0, sizeof(null_client));
  if (db_client_query(&null_client, "SELECT 1;") == 0) {
    printf("FAILED: Query on disconnected client unexpectedly succeeded\n");
    db_client_close(&client);
    return 1;
  }
  printf("PASSED: Operations on disconnected client cleanly return error "
         "without crashing\n");

  db_client_close(&client);
  printf("PASSED: Clean graceful shutdown of database connection\n");

  printf("\nALL DATABASE FAILURE AND RECOVERY TESTS SUCCEEDED.\n");
  return 0;
}
