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
    printf("Starting Transaction Reversal Tests...\n");

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

    db_client_query(&client, "UPDATE transactions SET reversal_of_id = NULL WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Reversal Org%');");
    db_client_query(&client, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Reversal Org%');");
    db_client_query(&client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Reversal Org%');");
    db_client_query(&client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Reversal Org%');");
    db_client_query(&client, "DELETE FROM organizations WHERE name LIKE 'Tx Reversal Org%';");

    long long org1 = create_test_org(&client, "Tx Reversal Org 1");
    long long org2 = create_test_org(&client, "Tx Reversal Org 2");
    EXPECT_TRUE(org1 > 0 && org2 > 0, "Create test organizations");

    long long user1 = create_test_user(&client, org1, "tx_rev_user1@org1.com");
    long long user2 = create_test_user(&client, org2, "tx_rev_user2@org2.com");
    EXPECT_TRUE(user1 > 0 && user2 > 0, "Create test users");

    Entity ent_a, ent_b;
    char err_buf[256];
    entity_service_create(&client, org1, user1, "Rev Ent A", ENTITY_TYPE_SUBSIDIARY, &ent_a, err_buf, sizeof(err_buf));
    entity_service_create(&client, org1, user1, "Rev Ent B", ENTITY_TYPE_VENDOR, &ent_b, err_buf, sizeof(err_buf));

    const char *key_orig = "90000000-0000-4000-8000-000000000001";
    Transaction t1;
    int is_replay = 0;
    int res = transaction_service_create(&client, org1, user1, ent_a.id, ent_b.id,
                                         "750.25", "INR", key_orig,
                                         &t1, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK && t1.id > 0, "Create standard transaction");

    const char *key_rev1 = "90000000-0000-4000-8000-000000000002";
    Transaction rev1;
    res = transaction_service_create_reversal(&client, org1, user1, t1.id, key_rev1,
                                              &rev1, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_INVALID_PARAM, "Reject reversal while original transaction is Pending");

    char mark_complete[256];
    snprintf(mark_complete, sizeof(mark_complete),
             "UPDATE transactions SET status = 'Completed' WHERE id = %lld", t1.id);
    db_client_query(&client, mark_complete);

    res = transaction_service_create_reversal(&client, org1, user1, t1.id, key_rev1,
                                              &rev1, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "Reversal creation succeeded for Completed transaction");
    EXPECT_TRUE(rev1.id > 0, "Reversal received valid ID");
    EXPECT_TRUE(rev1.transaction_type == TRANSACTION_TYPE_REVERSAL, "Transaction type is Reversal");
    EXPECT_TRUE(rev1.status == TRANSACTION_STATUS_PENDING, "Reversal status is Pending");
    EXPECT_TRUE(rev1.sender_entity_id == ent_b.id, "Sender entity swapped to original receiver");
    EXPECT_TRUE(rev1.receiver_entity_id == ent_a.id, "Receiver entity swapped to original sender");
    EXPECT_TRUE(rev1.amount_paise == t1.amount_paise, "Reversal amount paise matches original");
    EXPECT_TRUE(strcmp(rev1.amount_str, "750.25") == 0, "Reversal amount string matches original");
    EXPECT_TRUE(rev1.reversal_of_id == t1.id, "reversal_of_id points to original transaction");
    EXPECT_TRUE(is_replay == 0, "Reversal is first insertion");

    Transaction rev1_rep;
    res = transaction_service_create_reversal(&client, org1, user1, t1.id, key_rev1,
                                              &rev1_rep, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "Idempotent reversal retry succeeded");
    EXPECT_TRUE(is_replay == 1, "Idempotent reversal marked as replay");
    EXPECT_TRUE(rev1_rep.id == rev1.id, "Replay returns exact same reversal ID");

    const char *key_rev2 = "90000000-0000-4000-8000-000000000003";
    Transaction rev2;
    res = transaction_service_create_reversal(&client, org1, user1, t1.id, key_rev2,
                                              &rev2, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_CONFLICT, "Reject second reversal with different idempotency key");

    Transaction rev_not_found;
    res = transaction_service_create_reversal(&client, org1, user1, 777777LL, key_rev2,
                                              &rev_not_found, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_NOT_FOUND, "Non-existent transaction reversal returns NOT_FOUND");

    res = transaction_service_create_reversal(&client, org2, user2, t1.id, key_rev2,
                                              &rev_not_found, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_NOT_FOUND, "Cross-org transaction reversal returns NOT_FOUND");

    db_client_close(&client);

    printf("Transaction Reversal Test Complete: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
