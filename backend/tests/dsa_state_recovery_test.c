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

static long long g_idempotency_seq = 1000;
static long long insert_db_tx(DBClient *client, long long org_id, long long sender, long long receiver,
                              const char *amount_str, const char *status, const char *created_at) {
    char query[1024];
    char ik[64];
    char fp[65];
    snprintf(ik, sizeof(ik), "ik_recov_%lld_%lld", org_id, ++g_idempotency_seq);
    memset(fp, 'a', 64);
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
    printf("Starting DSA State and MySQL Recovery Tests...\n");

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

    long long org_id = create_test_org(&client, "DSA_Recovery_Test_Corp");
    EXPECT_TRUE(org_id > 0, "Create test organization for DSA recovery");

    cleanup_test_org(&client, org_id);
    org_id = create_test_org(&client, "DSA_Recovery_Test_Corp");

    long long e1 = create_test_entity(&client, org_id, "Entity_Alpha");
    long long e2 = create_test_entity(&client, org_id, "Entity_Beta");
    long long e3 = create_test_entity(&client, org_id, "Entity_Gamma");
    EXPECT_TRUE(e1 > 0 && e2 > 0 && e3 > 0, "Create entities Alpha, Beta, Gamma");

    long long tx1 = insert_db_tx(&client, org_id, e1, e2, "5000.00", "Completed", "2026-11-15 10:00:00");
    long long tx2 = insert_db_tx(&client, org_id, e2, e3, "2000.00", "Completed", "2026-11-15 10:01:00");
    long long tx3 = insert_db_tx(&client, org_id, e1, e3, "1500.00", "Pending", "2026-11-15 10:02:00");
    long long tx4 = insert_db_tx(&client, org_id, e2, e1, "750.00", "Pending", "2026-11-15 10:03:00");
    EXPECT_TRUE(tx1 > 0 && tx2 > 0 && tx3 > 0 && tx4 > 0, "Insert 2 Completed and 2 Pending transactions in MySQL");

    DsaState state;
    EXPECT_TRUE(dsa_state_init(&state, org_id) == 0, "Initialize DsaState for organization");

    EXPECT_TRUE(dsa_state_recover_from_db(&state, &client) == 0, "Recover DSA state from MySQL");
    EXPECT_TRUE(state.is_synchronized == 1, "State marked synchronized");

    int64_t bal_e1 = 0, bal_e2 = 0, bal_e3 = 0;
    EXPECT_TRUE(hashmap_get_balance(&state.entity_balances, e1, &bal_e1) == 0 && bal_e1 == -500000,
                "Entity Alpha balance is -5000.00 (-500000 paise)");
    EXPECT_TRUE(hashmap_get_balance(&state.entity_balances, e2, &bal_e2) == 0 && bal_e2 == 300000,
                "Entity Beta balance is +3000.00 (+300000 paise)");
    EXPECT_TRUE(hashmap_get_balance(&state.entity_balances, e3, &bal_e3) == 0 && bal_e3 == 200000,
                "Entity Gamma balance is +2000.00 (+200000 paise)");

    EXPECT_TRUE(queue_get_size(&state.pending_queue) == 2, "Pending queue has 2 items");
    Transaction q_peek;
    EXPECT_TRUE(queue_peek(&state.pending_queue, &q_peek) == 0 && q_peek.id == tx3, "Pending queue front is oldest pending (tx3)");

    EXPECT_TRUE(priority_queue_get_size(&state.pending_priority_queue) == 2, "Pending priority queue has 2 items");
    Transaction pq_peek;
    EXPECT_TRUE(priority_queue_peek(&state.pending_priority_queue, &pq_peek) == 0 && pq_peek.id == tx3 && pq_peek.amount_paise == 150000,
                "Priority queue max is highest amount pending (tx3, 1500.00)");

    EXPECT_TRUE(stack_get_size(&state.completed_stack) == 2, "Completed stack has 2 items");
    Transaction s_peek;
    EXPECT_TRUE(stack_peek(&state.completed_stack, &s_peek) == 0 && s_peek.id == tx2, "Completed stack top is newest completed (tx2)");

    EXPECT_TRUE(linked_list_get_size(&state.recent_transactions) == 4, "Recent list has all 4 transactions");

    dsa_state_destroy(&state);
    EXPECT_TRUE(state.is_synchronized == 0, "DsaState destroyed successfully (simulating system restart)");

    DsaState restarted_state;
    dsa_state_init(&restarted_state, org_id);
    EXPECT_TRUE(dsa_state_recover_from_db(&restarted_state, &client) == 0, "Re-recover state from MySQL on fresh instance");

    int64_t r_bal_e1 = 0;
    hashmap_get_balance(&restarted_state.entity_balances, e1, &r_bal_e1);
    EXPECT_TRUE(r_bal_e1 == -500000, "Alpha balance restored identically after restart");
    EXPECT_TRUE(queue_get_size(&restarted_state.pending_queue) == 2, "Queue restored identically after restart");
    EXPECT_TRUE(priority_queue_get_size(&restarted_state.pending_priority_queue) == 2, "Priority queue restored identically after restart");
    EXPECT_TRUE(stack_get_size(&restarted_state.completed_stack) == 2, "Completed stack restored identically after restart");

    dsa_state_destroy(&restarted_state);
    cleanup_test_org(&client, org_id);
    db_client_close(&client);

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
