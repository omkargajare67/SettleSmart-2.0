#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/entities/entity_service.h"
#include "../src/transactions/transaction_service.h"
#include "../src/websocket/event_dispatcher.h"
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

#define CONCURRENT_THREADS 6

typedef struct {
    DBConfig config;
    long long org_id;
    long long user_id;
    long long sender_id;
    long long receiver_id;
    char amount[32];
    char currency[8];
    char idempotency_key[65];
    Transaction out_tx;
    int is_replay;
    int result_code;
    char err_msg[256];
} ConcurrencyArg;

#ifdef _WIN32
static unsigned __stdcall worker_fn(void *arg) {
    ConcurrencyArg *t_arg = (ConcurrencyArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return 0;
    }

    t_arg->result_code = transaction_service_create(&client, t_arg->org_id, t_arg->user_id,
                                                    t_arg->sender_id, t_arg->receiver_id,
                                                    t_arg->amount, t_arg->currency,
                                                    t_arg->idempotency_key,
                                                    &t_arg->out_tx, &t_arg->is_replay,
                                                    t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return 0;
}
#else
static void *worker_fn(void *arg) {
    ConcurrencyArg *t_arg = (ConcurrencyArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return NULL;
    }

    t_arg->result_code = transaction_service_create(&client, t_arg->org_id, t_arg->user_id,
                                                    t_arg->sender_id, t_arg->receiver_id,
                                                    t_arg->amount, t_arg->currency,
                                                    t_arg->idempotency_key,
                                                    &t_arg->out_tx, &t_arg->is_replay,
                                                    t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return NULL;
}
#endif

static void cleanup_test_org(DBClient *client, long long org_id) {
    char query[512];
    snprintf(query, sizeof(query), "DELETE FROM event_outbox WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
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
    load_env_file(".env");
    load_env_file("../.env");
    DBConfig db_cfg = {
        .host = get_env_value("DB_HOST", "127.0.0.1"),
        .port = get_env_int("DB_PORT", 3307),
        .database = get_env_value("DB_NAME", "settlesmart_test"),
        .user = get_env_value("DB_USER", "settlesmart_user"),
        .password = get_env_value("DB_PASSWORD", "dev_password_123"),
        .ssl_mode = get_env_value("DB_SSL_MODE", "PREFERRED")
    };

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &db_cfg) != 0) {
        printf("Failed to connect to database\n");
        return 1;
    }

    char org_query[256];
    snprintf(org_query, sizeof(org_query),
             "INSERT INTO organizations (name, status) VALUES ('Org_Ws_Conc_Test', 'Active') "
             "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id);");
    db_client_query(&client, org_query);
    long long org_id = (long long)db_client_last_insert_id(&client);

    cleanup_test_org(&client, org_id);
    db_client_query(&client, org_query);
    org_id = (long long)db_client_last_insert_id(&client);

    char ent_query[512];
    snprintf(ent_query, sizeof(ent_query),
             "INSERT INTO entities (organization_id, name, entity_type, status) "
             "VALUES (%lld, 'Conc_Ent_1', 'Department', 'Active');", org_id);
    db_client_query(&client, ent_query);
    long long e1 = (long long)db_client_last_insert_id(&client);

    snprintf(ent_query, sizeof(ent_query),
             "INSERT INTO entities (organization_id, name, entity_type, status) "
             "VALUES (%lld, 'Conc_Ent_2', 'Department', 'Active');", org_id);
    db_client_query(&client, ent_query);
    long long e2 = (long long)db_client_last_insert_id(&client);

    char user_query[512];
    snprintf(user_query, sizeof(user_query),
             "INSERT INTO users (organization_id, email, password_hash, role, status) "
             "VALUES (%lld, 'conc_user@example.com', 'dummy_hash', 'Admin', 'Active');", org_id);
    db_client_query(&client, user_query);
    long long u_id = (long long)db_client_last_insert_id(&client);

    ConcurrencyArg args[CONCURRENT_THREADS];
#ifdef _WIN32
    HANDLE handles[CONCURRENT_THREADS];
#else
    pthread_t threads[CONCURRENT_THREADS];
#endif

    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        args[i].config = db_cfg;
        args[i].org_id = org_id;
        args[i].user_id = u_id;
        args[i].sender_id = e1;
        args[i].receiver_id = e2;
        snprintf(args[i].amount, sizeof(args[i].amount), "%d.00", 100 + i);
        snprintf(args[i].currency, sizeof(args[i].currency), "INR");
        snprintf(args[i].idempotency_key, sizeof(args[i].idempotency_key),
                 "e0000000-0000-4000-8000-00000000000%d", i + 1);
        args[i].is_replay = 0;
        args[i].result_code = -1;
        args[i].err_msg[0] = '\0';

#ifdef _WIN32
        handles[i] = (HANDLE)_beginthreadex(NULL, 0, worker_fn, &args[i], 0, NULL);
#else
        pthread_create(&threads[i], NULL, worker_fn, &args[i]);
#endif
    }

#ifdef _WIN32
    WaitForMultipleObjects(CONCURRENT_THREADS, handles, TRUE, INFINITE);
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        CloseHandle(handles[i]);
    }
#else
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    int all_succeeded = 1;
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        if (args[i].result_code != 0) {
            all_succeeded = 0;
            printf("Thread %d failed: %s\n", i, args[i].err_msg);
        }
    }
    EXPECT_TRUE(all_succeeded, "All concurrent transactions succeeded");

    char check_query[512];
    snprintf(check_query, sizeof(check_query),
             "SELECT event_sequence FROM event_outbox WHERE organization_id = %lld ORDER BY event_sequence ASC;",
             org_id);
    db_client_query(&client, check_query);
    MYSQL_RES *res = db_client_store_result(&client);
    EXPECT_TRUE(res != NULL, "Outbox events queried");

    int count = 0;
    int sequence_strictly_ordered = 1;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        count++;
        long long seq = atoll(row[0]);
        if (seq != (long long)count) {
            sequence_strictly_ordered = 0;
        }
    }
    db_client_free_result(res);

    EXPECT_TRUE(count == CONCURRENT_THREADS, "Outbox contains exactly 6 events");
    EXPECT_TRUE(sequence_strictly_ordered, "Outbox sequence numbers are strictly 1..N with no gaps and no duplicates");

    int64_t watermark = 0;
    outbox_get_watermark(&client, org_id, &watermark);
    EXPECT_TRUE(watermark == CONCURRENT_THREADS, "Organization watermark exactly matches total sequence count");

    cleanup_test_org(&client, org_id);
    db_client_close(&client);

    printf("WebSocket concurrency tests: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
