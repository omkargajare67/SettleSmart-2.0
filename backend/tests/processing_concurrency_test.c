#include "../src/transactions/transaction_processor.h"
#include "../src/transactions/transaction_service.h"
#include "../src/entities/entity_service.h"
#include "../src/auth/auth_service.h"
#include "../src/config/env_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

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

typedef struct {
    DBConfig db_cfg;
    long long organization_id;
    int processed_count;
} WorkerArgs;

#ifdef _WIN32
static unsigned __stdcall worker_func(void *arg) {
#else
static void *worker_func(void *arg) {
#endif
    WorkerArgs *wargs = (WorkerArgs *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &wargs->db_cfg) != 0) {
#ifdef _WIN32
        return 1;
#else
        return NULL;
#endif
    }

    wargs->processed_count = 0;
    char err_msg[256];

    while (1) {
        Transaction tx;
        int rc = transaction_processor_process_next(&client, NULL, NULL, wargs->organization_id, &tx, err_msg, sizeof(err_msg));
        if (rc == TRANSACTION_PROCESSOR_NO_PENDING) {
            break;
        }
        if (rc == TRANSACTION_PROCESSOR_OK) {
            wargs->processed_count++;
        }
    }

    db_client_close(&client);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int main(void) {
    printf("=== RUNNING TRANSACTION PROCESSING CONCURRENCY TESTS ===\n");

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
        printf("Failed to connect to database\n");
        return 1;
    }

    const char *org_name = "ProcConcurOrg";
    cleanup_org(&client, org_name);

    long long org_id = 0;
    long long user_id = 0;
    char err_msg[256];
    auth_register_organization(&client, org_name, "admin@concur.com",
                               "SecurePassword123!", &org_id, &user_id, err_msg, sizeof(err_msg));

    Entity ent_a, ent_b;
    entity_service_create(&client, org_id, user_id, "Entity Alpha", ENTITY_TYPE_VENDOR, &ent_a, err_msg, sizeof(err_msg));
    entity_service_create(&client, org_id, user_id, "Entity Beta", ENTITY_TYPE_CLIENT, &ent_b, err_msg, sizeof(err_msg));
    long long ent_a_id = ent_a.id;
    long long ent_b_id = ent_b.id;

    const int TOTAL_TXS = 20;
    for (int i = 0; i < TOTAL_TXS; i++) {
        char idem[64];
        snprintf(idem, sizeof(idem), "c0000000-0000-4000-8000-%012d", i);
        Transaction tx;
        int is_replay = 0;
        transaction_service_create(&client, org_id, user_id, ent_a_id, ent_b_id,
                                  "10.00", "INR", idem, &tx, &is_replay, err_msg, sizeof(err_msg));
    }

    const int NUM_THREADS = 4;
#ifdef _WIN32
    HANDLE threads[NUM_THREADS];
#else
    pthread_t threads[NUM_THREADS];
#endif
    WorkerArgs args[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].db_cfg = db_cfg;
        args[i].organization_id = org_id;
        args[i].processed_count = 0;
#ifdef _WIN32
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, worker_func, &args[i], 0, NULL);
#else
        pthread_create(&threads[i], NULL, worker_func, &args[i]);
#endif
    }

    int total_processed_by_workers = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
#ifdef _WIN32
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
#else
        pthread_join(threads[i], NULL);
#endif
        total_processed_by_workers += args[i].processed_count;
    }

    EXPECT_TRUE(total_processed_by_workers == TOTAL_TXS, "All 20 transactions processed across 4 worker threads");

    char count_query[512];
    snprintf(count_query, sizeof(count_query),
             "SELECT status, COUNT(*) FROM transactions WHERE organization_id = %lld GROUP BY status;",
             org_id);
    db_client_query(&client, count_query);
    MYSQL_RES *res = db_client_store_result(&client);
    EXPECT_TRUE(res != NULL, "Queried final transaction statuses");

    int completed_count = 0;
    int pending_count = 0;
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != NULL) {
            if (strcmp(row[0], "Completed") == 0) {
                completed_count = atoi(row[1]);
            } else if (strcmp(row[0], "Pending") == 0) {
                pending_count = atoi(row[1]);
            }
        }
        db_client_free_result(res);
    }

    EXPECT_TRUE(completed_count == TOTAL_TXS, "Database shows exactly 20 Completed transactions");
    EXPECT_TRUE(pending_count == 0, "Database shows exactly 0 Pending transactions remaining");

    char outbox_query[512];
    snprintf(outbox_query, sizeof(outbox_query),
             "SELECT COUNT(*) FROM event_outbox WHERE organization_id = %lld AND event_type = 'TRANSACTION_STATUS_CHANGED';",
             org_id);
    db_client_query(&client, outbox_query);
    MYSQL_RES *out_res = db_client_store_result(&client);
    if (out_res) {
        MYSQL_ROW row = mysql_fetch_row(out_res);
        int out_count = row ? atoi(row[0]) : 0;
        EXPECT_TRUE(out_count == TOTAL_TXS, "Outbox contains exactly 20 status changed events without duplicates");
        db_client_free_result(out_res);
    }

    cleanup_org(&client, org_name);
    db_client_close(&client);

    printf("\nConcurrency Processing Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
