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
#define CONFLICT_THREADS 4

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
} ConcurrencyTxArg;

#ifdef _WIN32
static unsigned __stdcall tx_worker(void *arg) {
    ConcurrencyTxArg *t_arg = (ConcurrencyTxArg *)arg;
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
static void *tx_worker(void *arg) {
    ConcurrencyTxArg *t_arg = (ConcurrencyTxArg *)arg;
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
    printf("Starting Transaction Concurrency and Stress Tests...\n");

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

    db_client_query(&client, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Concurrency Org%');");
    db_client_query(&client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Concurrency Org%');");
    db_client_query(&client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx Concurrency Org%');");
    db_client_query(&client, "DELETE FROM organizations WHERE name LIKE 'Tx Concurrency Org%';");

    long long org_id = create_test_org(&client, "Tx Concurrency Org");
    long long user_id = create_test_user(&client, org_id, "tx_concurrent_user@org.com");
    EXPECT_TRUE(org_id > 0 && user_id > 0, "Create test org and user");

    Entity ent_a, ent_b;
    char err_buf[256];
    entity_service_create(&client, org_id, user_id, "Concurrent A", ENTITY_TYPE_SUBSIDIARY, &ent_a, err_buf, sizeof(err_buf));
    entity_service_create(&client, org_id, user_id, "Concurrent B", ENTITY_TYPE_VENDOR, &ent_b, err_buf, sizeof(err_buf));

    const char *key_same = "c0c0c0c0-c0c0-4c0c-8c0c-c0c0c0c0c0c0";
    ConcurrencyTxArg args[CONCURRENT_THREADS];

#ifdef _WIN32
    HANDLE threads[CONCURRENT_THREADS];
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        args[i].config = db_cfg;
        args[i].org_id = org_id;
        args[i].user_id = user_id;
        args[i].sender_id = ent_a.id;
        args[i].receiver_id = ent_b.id;
        strcpy(args[i].amount, "1000.00");
        strcpy(args[i].currency, "INR");
        strcpy(args[i].idempotency_key, key_same);
        args[i].result_code = -1;
        args[i].is_replay = -1;
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, tx_worker, &args[i], 0, NULL);
    }
    WaitForMultipleObjects(CONCURRENT_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[CONCURRENT_THREADS];
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        args[i].config = db_cfg;
        args[i].org_id = org_id;
        args[i].user_id = user_id;
        args[i].sender_id = ent_a.id;
        args[i].receiver_id = ent_b.id;
        strcpy(args[i].amount, "1000.00");
        strcpy(args[i].currency, "INR");
        strcpy(args[i].idempotency_key, key_same);
        args[i].result_code = -1;
        args[i].is_replay = -1;
        pthread_create(&threads[i], NULL, tx_worker, &args[i]);
    }
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    int success_count = 0;
    int first_inserts = 0;
    int replays = 0;
    long long agreed_id = 0;

    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        if (args[i].result_code == TRANSACTION_ERR_OK) {
            success_count++;
            if (agreed_id == 0) {
                agreed_id = args[i].out_tx.id;
            } else {
                EXPECT_TRUE(args[i].out_tx.id == agreed_id, "Thread agreed on same transaction ID");
            }

            if (args[i].is_replay == 0) {
                first_inserts++;
            } else if (args[i].is_replay == 1) {
                replays++;
            }
        }
    }

    EXPECT_TRUE(success_count == CONCURRENT_THREADS, "All simultaneous identical threads succeeded");
    EXPECT_TRUE(first_inserts == 1, "Exactly one thread performed the initial insert");
    EXPECT_TRUE(replays == CONCURRENT_THREADS - 1, "Remaining threads received idempotent replays");

    char q[512];
    snprintf(q, sizeof(q),
             "SELECT COUNT(*) FROM transactions WHERE organization_id = %lld AND idempotency_key = '%s'",
             org_id, key_same);
    db_client_query(&client, q);
    MYSQL_RES *res = db_client_store_result(&client);
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[0]) {
            int cnt = atoi(row[0]);
            EXPECT_TRUE(cnt == 1, "Exactly one database row created for key_same");
        }
        db_client_free_result(res);
    }

    const char *key_conflict = "d0d0d0d0-d0d0-4d0d-8d0d-d0d0d0d0d0d0";
    ConcurrencyTxArg c_args[CONFLICT_THREADS];

#ifdef _WIN32
    HANDLE c_threads[CONFLICT_THREADS];
    for (int i = 0; i < CONFLICT_THREADS; i++) {
        c_args[i].config = db_cfg;
        c_args[i].org_id = org_id;
        c_args[i].user_id = user_id;
        c_args[i].sender_id = ent_a.id;
        c_args[i].receiver_id = ent_b.id;
        snprintf(c_args[i].amount, sizeof(c_args[i].amount), "%d00.00", i + 1);
        strcpy(c_args[i].currency, "INR");
        strcpy(c_args[i].idempotency_key, key_conflict);
        c_args[i].result_code = -1;
        c_args[i].is_replay = -1;
        c_threads[i] = (HANDLE)_beginthreadex(NULL, 0, tx_worker, &c_args[i], 0, NULL);
    }
    WaitForMultipleObjects(CONFLICT_THREADS, c_threads, TRUE, INFINITE);
    for (int i = 0; i < CONFLICT_THREADS; i++) {
        CloseHandle(c_threads[i]);
    }
#else
    pthread_t c_threads[CONFLICT_THREADS];
    for (int i = 0; i < CONFLICT_THREADS; i++) {
        c_args[i].config = db_cfg;
        c_args[i].org_id = org_id;
        c_args[i].user_id = user_id;
        c_args[i].sender_id = ent_a.id;
        c_args[i].receiver_id = ent_b.id;
        snprintf(c_args[i].amount, sizeof(c_args[i].amount), "%d00.00", i + 1);
        strcpy(c_args[i].currency, "INR");
        strcpy(c_args[i].idempotency_key, key_conflict);
        c_args[i].result_code = -1;
        c_args[i].is_replay = -1;
        pthread_create(&c_threads[i], NULL, tx_worker, &c_args[i]);
    }
    for (int i = 0; i < CONFLICT_THREADS; i++) {
        pthread_join(c_threads[i], NULL);
    }
#endif

    int c_success = 0;
    int c_conflicts = 0;
    for (int i = 0; i < CONFLICT_THREADS; i++) {
        if (c_args[i].result_code == TRANSACTION_ERR_OK && c_args[i].is_replay == 0) {
            c_success++;
        } else if (c_args[i].result_code == TRANSACTION_ERR_CONFLICT) {
            c_conflicts++;
        }
    }

    EXPECT_TRUE(c_success == 1, "Exactly one thread succeeded with unique payload under same key");
    EXPECT_TRUE(c_conflicts == CONFLICT_THREADS - 1, "Remaining threads received 409 Conflict");

    db_client_close(&client);

    printf("Transaction Concurrency Test Complete: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
