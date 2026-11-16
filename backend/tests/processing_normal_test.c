#include "../src/transactions/transaction_processor.h"
#include "../src/transactions/transaction_service.h"
#include "../src/entities/entity_service.h"
#include "../src/auth/auth_service.h"
#include "../src/config/env_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define EXPECT_TRUE(cond, desc) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", desc); \
    } else { \
        printf("  [FAIL] %s (line %d)\n", desc, __LINE__); \
    } \
} while(0)

static void cleanup_org(DBClient *client, const char *org_name) {
    char query[512];
    snprintf(query, sizeof(query), "SELECT id FROM organizations WHERE name = '%s';", org_name);
    if (db_client_query(client, query) == 0) {
        MYSQL_RES *res = db_client_store_result(client);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                long long org_id = atoll(row[0]);
                char del[512];
                snprintf(del, sizeof(del), "DELETE FROM event_outbox WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM transactions WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM entities WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM sessions WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM users WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM organizations WHERE id = %lld;", org_id);
                db_client_query(client, del);
            }
            db_client_free_result(res);
        }
    }
}

int main(void) {
    printf("=== RUNNING TRANSACTION PROCESSING NORMAL WORKFLOW TESTS ===\n");

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
        printf("Failed to connect to database: %s\n", db_client_get_error(&client));
        return 1;
    }

    const char *org_name = "ProcNormalOrg";
    cleanup_org(&client, org_name);

    long long org_id = 0;
    long long user_id = 0;
    char err_msg[256];
    int reg_rc = auth_register_organization(&client, org_name, "admin@procnormal.com",
                                            "SecurePassword123!", &org_id, &user_id, err_msg, sizeof(err_msg));
    EXPECT_TRUE(reg_rc == 0, "Register organization for normal processing");

    Entity ent_a, ent_b;
    entity_service_create(&client, org_id, user_id, "Entity A", ENTITY_TYPE_VENDOR, &ent_a, err_msg, sizeof(err_msg));
    entity_service_create(&client, org_id, user_id, "Entity B", ENTITY_TYPE_CLIENT, &ent_b, err_msg, sizeof(err_msg));
    long long ent_a_id = ent_a.id;
    long long ent_b_id = ent_b.id;
    EXPECT_TRUE(ent_a_id > 0 && ent_b_id > 0, "Entities created successfully");

    Transaction tx1, tx2;
    int is_replay = 0;
    int c1 = transaction_service_create(&client, org_id, user_id, ent_a_id, ent_b_id,
                                        "25.00", "INR", "a0000000-0000-4000-8000-000000000001", &tx1, &is_replay, err_msg, sizeof(err_msg));
    int c2 = transaction_service_create(&client, org_id, user_id, ent_a_id, ent_b_id,
                                        "15.00", "INR", "a0000000-0000-4000-8000-000000000002", &tx2, &is_replay, err_msg, sizeof(err_msg));
    EXPECT_TRUE(c1 == TRANSACTION_ERR_OK && c2 == TRANSACTION_ERR_OK, "Created 2 pending transactions");
    EXPECT_TRUE(tx1.status == TRANSACTION_STATUS_PENDING && tx2.status == TRANSACTION_STATUS_PENDING, "Both transactions pending");

    DsaState state;
    dsa_state_init(&state, org_id);
    dsa_state_recover_from_db(&state, &client);
    EXPECT_TRUE(queue_get_size(&state.pending_queue) == 2, "Pending queue has 2 transactions");

    Transaction proc1;
    int p1 = transaction_processor_process_next(&client, &state, NULL, org_id, &proc1, err_msg, sizeof(err_msg));
    EXPECT_TRUE(p1 == TRANSACTION_PROCESSOR_OK, "Process next transaction 1 succeeds");
    EXPECT_TRUE(proc1.id == tx1.id, "FIFO order processed oldest transaction tx1 first");
    EXPECT_TRUE(proc1.status == TRANSACTION_STATUS_COMPLETED, "Transaction 1 marked Completed");

    int64_t bal_a = 0;
    int64_t bal_b = 0;
    hashmap_get_balance(&state.entity_balances, ent_a_id, &bal_a);
    hashmap_get_balance(&state.entity_balances, ent_b_id, &bal_b);
    EXPECT_TRUE(bal_a == -2500, "Sender balance debited 2500 paise");
    EXPECT_TRUE(bal_b == 2500, "Receiver balance credited 2500 paise");
    EXPECT_TRUE(queue_get_size(&state.pending_queue) == 1, "Pending queue decreased to 1");

    Transaction proc2;
    int p2 = transaction_processor_process_next(&client, &state, NULL, org_id, &proc2, err_msg, sizeof(err_msg));
    EXPECT_TRUE(p2 == TRANSACTION_PROCESSOR_OK, "Process next transaction 2 succeeds");
    EXPECT_TRUE(proc2.id == tx2.id, "FIFO order processed tx2");
    EXPECT_TRUE(proc2.status == TRANSACTION_STATUS_COMPLETED, "Transaction 2 marked Completed");

    hashmap_get_balance(&state.entity_balances, ent_a_id, &bal_a);
    hashmap_get_balance(&state.entity_balances, ent_b_id, &bal_b);
    EXPECT_TRUE(bal_a == -4000, "Sender balance cumulative -4000 paise");
    EXPECT_TRUE(bal_b == 4000, "Receiver balance cumulative +4000 paise");
    EXPECT_TRUE(queue_get_size(&state.pending_queue) == 0, "Pending queue now empty");

    Transaction proc3;
    int p3 = transaction_processor_process_next(&client, &state, NULL, org_id, &proc3, err_msg, sizeof(err_msg));
    EXPECT_TRUE(p3 == TRANSACTION_PROCESSOR_NO_PENDING, "Process next returns NO_PENDING when queue empty");

    Transaction dup_proc;
    int p_dup = transaction_processor_process_tx(&client, &state, NULL, org_id, tx1.id, &dup_proc, err_msg, sizeof(err_msg));
    EXPECT_TRUE(p_dup == TRANSACTION_PROCESSOR_ERR_INVALID_STATUS, "Processing already completed transaction fails with INVALID_STATUS");

    char deact_query[512];
    snprintf(deact_query, sizeof(deact_query), "UPDATE entities SET status = 'Inactive' WHERE id = %lld;", ent_b_id);
    db_client_query(&client, deact_query);

    snprintf(deact_query, sizeof(deact_query),
             "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, idempotency_key, request_fingerprint, created_by_user_id) "
             "VALUES (%lld, %lld, %lld, 10.00, 'INR', 'Pending', 'Standard', 'a0000000-0000-4000-8000-000000000003', 'dummy-fingerprint', %lld);",
             org_id, ent_a_id, ent_b_id, user_id);
    db_client_query(&client, deact_query);
    long long tx3_id = (long long)db_client_last_insert_id(&client);

    Transaction proc_fail;
    int p_fail = transaction_processor_process_tx(&client, &state, NULL, org_id, tx3_id, &proc_fail, err_msg, sizeof(err_msg));
    EXPECT_TRUE(p_fail == TRANSACTION_PROCESSOR_ERR_INACTIVE_ENTITY, "Processing fails with INACTIVE_ENTITY when participant is inactive");

    Transaction verify_tx3;
    transaction_service_get_by_id(&client, org_id, tx3_id, &verify_tx3, err_msg, sizeof(err_msg));
    EXPECT_TRUE(verify_tx3.status == TRANSACTION_STATUS_PENDING, "Failed transaction was rolled back and remains Pending");

    dsa_state_destroy(&state);
    cleanup_org(&client, org_name);
    db_client_close(&client);

    printf("\nNormal Processing Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
