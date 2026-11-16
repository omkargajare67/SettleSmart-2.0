#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/dsa/dsa_state.h"
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
    if (db_client_query(client, query) != 0) return -1;
    return (long long)db_client_last_insert_id(client);
}

static long long create_test_entity(DBClient *client, long long org_id, const char *name) {
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO entities (organization_id, name, entity_type, status) "
             "VALUES (%lld, '%s', 'Department', 'Active');", org_id, name);
    if (db_client_query(client, query) != 0) return -1;
    return (long long)db_client_last_insert_id(client);
}

static long long g_multi_org_ik_seq = 5000;
static long long insert_db_tx(DBClient *client, long long org_id, long long sender, long long receiver,
                              const char *amount_str, const char *status, const char *created_at) {
    char query[1024];
    char ik[64];
    char fp[65];
    snprintf(ik, sizeof(ik), "ik_multi_%lld_%lld", org_id, ++g_multi_org_ik_seq);
    memset(fp, 'b', 64);
    fp[64] = '\0';

    snprintf(query, sizeof(query),
             "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, "
             "amount, currency, status, transaction_type, idempotency_key, request_fingerprint, created_at) "
             "VALUES (%lld, %lld, %lld, %s, 'INR', '%s', 'Standard', '%s', '%s', '%s');",
             org_id, sender, receiver, amount_str, status, ik, fp, created_at);
    if (db_client_query(client, query) != 0) return -1;
    return (long long)db_client_last_insert_id(client);
}

static void cleanup_test_org(DBClient *client, long long org_id) {
    char query[512];
    snprintf(query, sizeof(query), "UPDATE transactions SET reversal_of_id = NULL WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM transactions WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM entities WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM users WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM organizations WHERE id = %lld;", org_id);
    db_client_query(client, query);
}

int main(void) {
    printf("Starting DSA Multi-Organization Isolation Tests...\n");

    load_env_file(".env");
    load_env_file("../.env");

    const char *host = get_env_value("DB_HOST", "127.0.0.1");
    int port = get_env_int("DB_PORT", 3307);
    const char *db = get_env_value("DB_NAME", "settlesmart_test");
    const char *user = get_env_value("DB_USER", "settlesmart_user");
    const char *pass = get_env_value("DB_PASS", "dev_password_123");

    DBClient client;
    db_client_init(&client);
    DBConfig config = { host, port, db, user, pass, NULL };
    if (db_client_connect(&client, &config) != 0) {
        printf("Failed to connect to database: %s\n", db_client_get_error(&client));
        return 1;
    }

    long long org1 = create_test_org(&client, "Org_Tenancy_A");
    long long org2 = create_test_org(&client, "Org_Tenancy_B");
    EXPECT_TRUE(org1 > 0 && org2 > 0, "Create organizations A and B");

    cleanup_test_org(&client, org1);
    cleanup_test_org(&client, org2);
    org1 = create_test_org(&client, "Org_Tenancy_A");
    org2 = create_test_org(&client, "Org_Tenancy_B");

    long long e1_a = create_test_entity(&client, org1, "Entity_A1");
    long long e2_a = create_test_entity(&client, org1, "Entity_A2");

    long long e1_b = create_test_entity(&client, org2, "Entity_B1");
    long long e2_b = create_test_entity(&client, org2, "Entity_B2");

    long long tx_a = insert_db_tx(&client, org1, e1_a, e2_a, "1000.00", "Completed", "2026-11-15 10:00:00");
    long long tx_b = insert_db_tx(&client, org2, e1_b, e2_b, "2000.00", "Pending", "2026-11-15 10:05:00");
    EXPECT_TRUE(tx_a > 0 && tx_b > 0, "Insert transactions for org A and org B");

    DsaState state1, state2;
    dsa_state_init(&state1, org1);
    dsa_state_init(&state2, org2);

    EXPECT_TRUE(dsa_state_recover_from_db(&state1, &client) == 0, "Recover Org 1 state");
    EXPECT_TRUE(dsa_state_recover_from_db(&state2, &client) == 0, "Recover Org 2 state");

    int64_t dummy = 0;
    EXPECT_TRUE(hashmap_get_balance(&state1.entity_balances, e1_a, &dummy) == 0, "Org 1 has entity A1");
    EXPECT_TRUE(hashmap_get_balance(&state1.entity_balances, e1_b, &dummy) == -1, "Org 1 does NOT have entity B1");

    EXPECT_TRUE(hashmap_get_balance(&state2.entity_balances, e1_b, &dummy) == 0, "Org 2 has entity B1");
    EXPECT_TRUE(hashmap_get_balance(&state2.entity_balances, e1_a, &dummy) == -1, "Org 2 does NOT have entity A1");

    EXPECT_TRUE(queue_get_size(&state1.pending_queue) == 0, "Org 1 pending queue has 0 items (only completed tx)");
    EXPECT_TRUE(queue_get_size(&state2.pending_queue) == 1, "Org 2 pending queue has 1 item");

    Transaction foreign_tx;
    memset(&foreign_tx, 0, sizeof(Transaction));
    foreign_tx.id = 9999;
    foreign_tx.organization_id = org2;
    foreign_tx.sender_entity_id = e1_b;
    foreign_tx.receiver_entity_id = e2_b;
    foreign_tx.amount_paise = 50000;
    foreign_tx.status = TRANSACTION_STATUS_PENDING;

    EXPECT_TRUE(dsa_state_on_transaction_created(&state1, &foreign_tx) == -1,
                "Cross-organization transaction insertion on State 1 is rejected");
    EXPECT_TRUE(queue_get_size(&state1.pending_queue) == 0, "State 1 pending queue remains empty after rejected cross-org attempt");

    dsa_state_destroy(&state1);
    dsa_state_destroy(&state2);
    cleanup_test_org(&client, org1);
    cleanup_test_org(&client, org2);
    db_client_close(&client);

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
