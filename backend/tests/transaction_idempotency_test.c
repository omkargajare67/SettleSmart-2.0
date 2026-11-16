#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/entities/entity_service.h"
#include "../src/transactions/transaction_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define EXPECT_TRUE(cond, name) do { \
    if (cond) { \
        printf("PASS: %s\n", name); \
        g_tests_passed++; \
    } else { \
        printf("FAIL: %s\n", name); \
        g_tests_failed++; \
    } \
} while(0)

static long long create_test_org(DBClient *client, const char *name) {
    char query[256];
    snprintf(query, sizeof(query),
             "INSERT INTO organizations (name, status) VALUES ('%s', 'Active') "
             "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id);", name);
    if (db_client_query(client, query) != 0) {
        return -1;
    }
    return (long long)db_client_last_insert_id(client);
}

static long long create_test_user(DBClient *client, long long org_id, const char *email) {
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO users (organization_id, email, password_hash, role, status) "
             "VALUES (%lld, '%s', 'hash', 'Admin', 'Active') "
             "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id);", org_id, email);
    if (db_client_query(client, query) != 0) {
        return -1;
    }
    return (long long)db_client_last_insert_id(client);
}

int main(void) {
    printf("Starting Transaction Idempotency Tests...\n");

    load_env_file(".env");
    load_env_file("../.env");

    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &db_cfg) != 0) {
        printf("FAIL: Database connection failed: %s\n", db_client_get_error(&client));
        return 1;
    }

    db_client_query(&client, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Idem Org%');");
    db_client_query(&client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Idem Org%');");
    db_client_query(&client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Idem Org%');");
    db_client_query(&client, "DELETE FROM organizations WHERE name LIKE 'Idem Org%';");

    long long org1 = create_test_org(&client, "Idem Org 1");
    long long org2 = create_test_org(&client, "Idem Org 2");
    EXPECT_TRUE(org1 > 0 && org2 > 0, "Create organizations");

    long long user1 = create_test_user(&client, org1, "idem_user1@org1.com");
    long long user2 = create_test_user(&client, org2, "idem_user2@org2.com");
    EXPECT_TRUE(user1 > 0 && user2 > 0, "Create users");

    Entity e1_a, e1_b, e2_a, e2_b;
    char err_buf[256];

    entity_service_create(&client, org1, user1, "Idem 1A", ENTITY_TYPE_SUBSIDIARY, &e1_a, err_buf, sizeof(err_buf));
    entity_service_create(&client, org1, user1, "Idem 1B", ENTITY_TYPE_VENDOR, &e1_b, err_buf, sizeof(err_buf));
    entity_service_create(&client, org2, user2, "Idem 2A", ENTITY_TYPE_SUBSIDIARY, &e2_a, err_buf, sizeof(err_buf));
    entity_service_create(&client, org2, user2, "Idem 2B", ENTITY_TYPE_VENDOR, &e2_b, err_buf, sizeof(err_buf));

    const char *key1 = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    Transaction tx1;
    int is_replay = 0;

    int res = transaction_service_create(&client, org1, user1, e1_a.id, e1_b.id,
                                         "2500.00", "INR", key1,
                                         &tx1, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "First submission succeeded");
    EXPECT_TRUE(is_replay == 0, "First submission is not a replay");
    long long tx1_id = tx1.id;
    EXPECT_TRUE(tx1_id > 0, "First transaction received valid ID");

    Transaction tx1_repeat;
    res = transaction_service_create(&client, org1, user1, e1_a.id, e1_b.id,
                                     "2500.00", "INR", key1,
                                     &tx1_repeat, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "Identical retry succeeded");
    EXPECT_TRUE(is_replay == 1, "Identical retry marked as idempotent replay");
    EXPECT_TRUE(tx1_repeat.id == tx1_id, "Replay returns exact same transaction ID");
    EXPECT_TRUE(tx1_repeat.amount_paise == tx1.amount_paise, "Replay returns identical amount");

    char count_q[512];
    snprintf(count_q, sizeof(count_q),
             "SELECT COUNT(*) FROM transactions WHERE organization_id = %lld AND idempotency_key = '%s'",
             org1, key1);
    db_client_query(&client, count_q);
    MYSQL_RES *cnt_res = db_client_store_result(&client);
    if (cnt_res) {
        MYSQL_ROW row = mysql_fetch_row(cnt_res);
        if (row && row[0]) {
            int cnt = atoi(row[0]);
            EXPECT_TRUE(cnt == 1, "Only 1 database row exists for key1 in Org 1");
        }
        db_client_free_result(cnt_res);
    }

    Transaction tx1_conflict;
    res = transaction_service_create(&client, org1, user1, e1_a.id, e1_b.id,
                                     "3500.00", "INR", key1,
                                     &tx1_conflict, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_CONFLICT, "Reject modified payload with same idempotency key (different amount)");

    res = transaction_service_create(&client, org1, user1, e1_b.id, e1_a.id,
                                     "2500.00", "INR", key1,
                                     &tx1_conflict, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_CONFLICT, "Reject modified payload with same idempotency key (swapped entities)");

    Transaction tx2_org2;
    res = transaction_service_create(&client, org2, user2, e2_a.id, e2_b.id,
                                     "2500.00", "INR", key1,
                                     &tx2_org2, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "Same key in Org 2 succeeds independently");
    EXPECT_TRUE(is_replay == 0, "Org 2 transaction is not a replay");
    EXPECT_TRUE(tx2_org2.id != tx1_id, "Org 2 receives separate transaction ID");
    EXPECT_TRUE(tx2_org2.organization_id == org2, "Org 2 transaction belongs to Org 2");

    db_client_close(&client);

    printf("Transaction Idempotency Test Complete: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
