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
    printf("=== RUNNING TRANSACTION PROCESSING REVERSAL WORKFLOW TESTS ===\n");

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

    const char *org_name = "ProcReversalOrg";
    cleanup_org(&client, org_name);

    long long org_id = 0;
    long long user_id = 0;
    char err_msg[256];
    auth_register_organization(&client, org_name, "admin@reversal.com",
                               "SecurePassword123!", &org_id, &user_id, err_msg, sizeof(err_msg));

    Entity ent_a, ent_b;
    entity_service_create(&client, org_id, user_id, "Entity Alpha", ENTITY_TYPE_VENDOR, &ent_a, err_msg, sizeof(err_msg));
    entity_service_create(&client, org_id, user_id, "Entity Beta", ENTITY_TYPE_CLIENT, &ent_b, err_msg, sizeof(err_msg));
    long long ent_a_id = ent_a.id;
    long long ent_b_id = ent_b.id;

    Transaction orig_tx;
    int is_replay = 0;
    transaction_service_create(&client, org_id, user_id, ent_a_id, ent_b_id,
                              "50.00", "INR", "b0000000-0000-4000-8000-000000000001", &orig_tx, &is_replay, err_msg, sizeof(err_msg));

    DsaState state;
    dsa_state_init(&state, org_id);
    dsa_state_recover_from_db(&state, &client);

    Transaction proc_orig;
    int p_orig = transaction_processor_process_tx(&client, &state, NULL, org_id, orig_tx.id, &proc_orig, err_msg, sizeof(err_msg));
    EXPECT_TRUE(p_orig == TRANSACTION_PROCESSOR_OK, "Original transaction processed to Completed");

    int64_t bal_a = 0, bal_b = 0;
    hashmap_get_balance(&state.entity_balances, ent_a_id, &bal_a);
    hashmap_get_balance(&state.entity_balances, ent_b_id, &bal_b);
    EXPECT_TRUE(bal_a == -5000 && bal_b == 5000, "Initial balances debited -5000 and credited +5000");

    Transaction rev_tx;
    int c_rev = transaction_service_create_reversal(&client, org_id, user_id, orig_tx.id, "b0000000-0000-4000-8000-000000000002", &rev_tx, &is_replay, err_msg, sizeof(err_msg));
    EXPECT_TRUE(c_rev == TRANSACTION_ERR_OK, "Created reversal transaction");
    EXPECT_TRUE(rev_tx.reversal_of_id == orig_tx.id, "Reversal references original transaction id");
    EXPECT_TRUE(rev_tx.sender_entity_id == ent_b_id && rev_tx.receiver_entity_id == ent_a_id, "Reversal inverts sender and receiver");

    dsa_state_on_transaction_created(&state, &rev_tx);

    Transaction proc_rev;
    int p_rev = transaction_processor_process_tx(&client, &state, NULL, org_id, rev_tx.id, &proc_rev, err_msg, sizeof(err_msg));
    EXPECT_TRUE(p_rev == TRANSACTION_PROCESSOR_OK, "Reversal transaction processed successfully");

    Transaction check_orig, check_rev;
    transaction_service_get_by_id(&client, org_id, orig_tx.id, &check_orig, err_msg, sizeof(err_msg));
    transaction_service_get_by_id(&client, org_id, rev_tx.id, &check_rev, err_msg, sizeof(err_msg));

    EXPECT_TRUE(check_orig.status == TRANSACTION_STATUS_REVERSED, "Original transaction marked Reversed in DB");
    EXPECT_TRUE(check_rev.status == TRANSACTION_STATUS_COMPLETED, "Reversal transaction marked Completed in DB");

    hashmap_get_balance(&state.entity_balances, ent_a_id, &bal_a);
    hashmap_get_balance(&state.entity_balances, ent_b_id, &bal_b);
    EXPECT_TRUE(bal_a == 0, "In-memory Entity A balance neutralized to exactly 0");
    EXPECT_TRUE(bal_b == 0, "In-memory Entity B balance neutralized to exactly 0");

    DsaState restart_state;
    dsa_state_init(&restart_state, org_id);
    dsa_state_recover_from_db(&restart_state, &client);

    int64_t rec_a = 0, rec_b = 0;
    hashmap_get_balance(&restart_state.entity_balances, ent_a_id, &rec_a);
    hashmap_get_balance(&restart_state.entity_balances, ent_b_id, &rec_b);
    EXPECT_TRUE(rec_a == 0, "Database recovery recovers Entity A balance to exactly 0");
    EXPECT_TRUE(rec_b == 0, "Database recovery recovers Entity B balance to exactly 0");

    char outbox_query[512];
    snprintf(outbox_query, sizeof(outbox_query),
             "SELECT COUNT(*) FROM event_outbox WHERE organization_id = %lld AND event_type = 'TRANSACTION_STATUS_CHANGED';",
             org_id);
    db_client_query(&client, outbox_query);
    MYSQL_RES *out_res = db_client_store_result(&client);
    EXPECT_TRUE(out_res != NULL, "Queried event outbox for status changed events");
    if (out_res) {
        MYSQL_ROW row = mysql_fetch_row(out_res);
        int out_count = row ? atoi(row[0]) : 0;
        EXPECT_TRUE(out_count >= 3, "Outbox recorded status change events for original and reversal transitions");
        db_client_free_result(out_res);
    }

    Transaction rev_again;
    int c_again = transaction_service_create_reversal(&client, org_id, user_id, orig_tx.id, "b0000000-0000-4000-8000-000000000003", &rev_again, &is_replay, err_msg, sizeof(err_msg));
    EXPECT_TRUE(c_again == TRANSACTION_ERR_INVALID_PARAM || c_again == TRANSACTION_ERR_CONFLICT, "Cannot reverse an already reversed transaction");

    dsa_state_destroy(&state);
    dsa_state_destroy(&restart_state);
    cleanup_org(&client, org_name);
    db_client_close(&client);

    printf("\nReversal Processing Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
