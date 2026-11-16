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
    printf("Starting Transaction CRUD and Persistence Tests...\n");

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

    db_client_query(&client, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Crud Org%');");
    db_client_query(&client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Crud Org%');");
    db_client_query(&client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Crud Org%');");
    db_client_query(&client, "DELETE FROM organizations WHERE name LIKE 'Tx Crud Org%';");

    long long org1 = create_test_org(&client, "Tx Crud Org 1");
    long long org2 = create_test_org(&client, "Tx Crud Org 2");
    EXPECT_TRUE(org1 > 0 && org2 > 0, "Create test organizations");

    long long user1 = create_test_user(&client, org1, "tx_crud_user1@org1.com");
    long long user2 = create_test_user(&client, org2, "tx_crud_user2@org2.com");
    EXPECT_TRUE(user1 > 0 && user2 > 0, "Create test users");

    Entity ent_a, ent_b, ent_c;
    char err_buf[256];

    int res = entity_service_create(&client, org1, user1, "Entity A Active", ENTITY_TYPE_SUBSIDIARY, &ent_a, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == ENTITY_ERR_OK && ent_a.id > 0, "Create Entity A Active");

    res = entity_service_create(&client, org1, user1, "Entity B Active", ENTITY_TYPE_VENDOR, &ent_b, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == ENTITY_ERR_OK && ent_b.id > 0, "Create Entity B Active");

    res = entity_service_create(&client, org1, user1, "Entity C Inactive", ENTITY_TYPE_CLIENT, &ent_c, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == ENTITY_ERR_OK && ent_c.id > 0, "Create Entity C");

    Entity ent_c_updated;
    res = entity_service_update_status(&client, org1, user1, ent_c.id, ENTITY_STATUS_INACTIVE, &ent_c_updated, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == ENTITY_ERR_OK && ent_c_updated.status == ENTITY_STATUS_INACTIVE, "Deactivate Entity C");

    Transaction tx1;
    int is_replay = 0;
    const char *key1 = "11111111-1111-4111-8111-111111111111";

    res = transaction_service_create(&client, org1, user1, ent_a.id, ent_b.id,
                                     "1500.50", "INR", key1,
                                     &tx1, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "Create valid standard transaction");
    EXPECT_TRUE(tx1.id > 0, "Transaction received positive auto-increment ID");
    EXPECT_TRUE(tx1.organization_id == org1, "Transaction organization matches");
    EXPECT_TRUE(tx1.sender_entity_id == ent_a.id, "Sender entity matches");
    EXPECT_TRUE(tx1.receiver_entity_id == ent_b.id, "Receiver entity matches");
    EXPECT_TRUE(tx1.amount_paise == 150050, "Amount paise precision exact");
    EXPECT_TRUE(strcmp(tx1.amount_str, "1500.50") == 0, "Amount string matches");
    EXPECT_TRUE(strcmp(tx1.currency, "INR") == 0, "Currency is INR");
    EXPECT_TRUE(tx1.status == TRANSACTION_STATUS_PENDING, "Initial status is Pending");
    EXPECT_TRUE(tx1.transaction_type == TRANSACTION_TYPE_STANDARD, "Type is Standard");
    EXPECT_TRUE(tx1.created_by_user_id == user1, "Creator user ID recorded");
    EXPECT_TRUE(is_replay == 0, "Not an idempotent replay");

    Transaction tx_inactive;
    const char *key2 = "22222222-2222-4222-8222-222222222222";
    res = transaction_service_create(&client, org1, user1, ent_c.id, ent_b.id,
                                     "500.00", "INR", key2,
                                     &tx_inactive, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_INACTIVE_ENTITY, "Reject transaction with inactive sender");

    const char *key3 = "33333333-3333-4333-8333-333333333333";
    res = transaction_service_create(&client, org1, user1, ent_a.id, ent_c.id,
                                     "500.00", "INR", key3,
                                     &tx_inactive, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_INACTIVE_ENTITY, "Reject transaction with inactive receiver");

    const char *key4 = "44444444-4444-4444-8444-444444444444";
    res = transaction_service_create(&client, org1, user1, ent_a.id, ent_a.id,
                                     "500.00", "INR", key4,
                                     &tx_inactive, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_INVALID_PARAM, "Reject self-transaction (sender == receiver)");

    const char *key5 = "55555555-5555-4555-8555-555555555555";
    res = transaction_service_create(&client, org1, user1, 999999LL, ent_b.id,
                                     "500.00", "INR", key5,
                                     &tx_inactive, &is_replay, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_NOT_FOUND, "Reject transaction with non-existent entity");

    Transaction fetched;
    res = transaction_service_get_by_id(&client, org1, tx1.id, &fetched, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "Fetch transaction by ID");
    EXPECT_TRUE(fetched.id == tx1.id, "Fetched ID matches");
    EXPECT_TRUE(fetched.amount_paise == 150050, "Fetched paise matches");
    EXPECT_TRUE(strcmp(fetched.amount_str, "1500.50") == 0, "Fetched amount string matches");

    res = transaction_service_get_by_id(&client, org2, tx1.id, &fetched, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_NOT_FOUND, "Cross-organization get_by_id returns NOT_FOUND");

    res = transaction_service_get_by_id(&client, org1, 888888LL, &fetched, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_NOT_FOUND, "Non-existent transaction returns NOT_FOUND");

    db_client_close(&client);

    printf("Transaction CRUD Test Complete: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
