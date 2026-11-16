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

#define CONCURRENT_WORKERS 4

typedef struct {
    DBConfig config;
    long long org_id;
    long long user_id;
    int result_code;
    char err_msg[256];
} ConcurrencyEntityArg;

#ifdef _WIN32
static unsigned __stdcall create_worker(void *arg) {
    ConcurrencyEntityArg *t_arg = (ConcurrencyEntityArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return 0;
    }

    Entity created;
    t_arg->result_code = entity_service_create(&client, t_arg->org_id, t_arg->user_id,
                                              "Concurrent Corp", ENTITY_TYPE_VENDOR,
                                              &created, t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return 0;
}
#else
static void *create_worker(void *arg) {
    ConcurrencyEntityArg *t_arg = (ConcurrencyEntityArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return NULL;
    }

    Entity created;
    t_arg->result_code = entity_service_create(&client, t_arg->org_id, t_arg->user_id,
                                              "Concurrent Corp", ENTITY_TYPE_VENDOR,
                                              &created, t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return NULL;
}
#endif

int main(void) {
    printf("Starting Entity Concurrency Tests...\n");

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

    char org_query[256];
    snprintf(org_query, sizeof(org_query),
             "INSERT INTO organizations (name, status) VALUES ('Entity Concurrency Org', 'Active') "
             "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id);");
    db_client_query(&client, org_query);
    long long org_id = (long long)db_client_last_insert_id(&client);

    char user_query[512];
    snprintf(user_query, sizeof(user_query),
             "INSERT INTO users (organization_id, email, password_hash, role, status) "
             "VALUES (%lld, 'admin_conc@test.com', 'hash', 'Admin', 'Active') "
             "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id);", org_id);
    db_client_query(&client, user_query);
    long long user_id = (long long)db_client_last_insert_id(&client);

    char clean[256];
    snprintf(clean, sizeof(clean),
             "DELETE FROM entities WHERE organization_id = %lld AND name = 'Concurrent Corp';",
             org_id);
    db_client_query(&client, clean);

    ConcurrencyEntityArg args[CONCURRENT_WORKERS];
    for (int i = 0; i < CONCURRENT_WORKERS; i++) {
        args[i].config = db_cfg;
        args[i].org_id = org_id;
        args[i].user_id = user_id;
        args[i].result_code = -1;
        args[i].err_msg[0] = '\0';
    }

#ifdef _WIN32
    HANDLE threads[CONCURRENT_WORKERS];
    for (int i = 0; i < CONCURRENT_WORKERS; i++) {
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, create_worker, &args[i], 0, NULL);
    }
    WaitForMultipleObjects(CONCURRENT_WORKERS, threads, TRUE, INFINITE);
    for (int i = 0; i < CONCURRENT_WORKERS; i++) {
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[CONCURRENT_WORKERS];
    for (int i = 0; i < CONCURRENT_WORKERS; i++) {
        pthread_create(&threads[i], NULL, create_worker, &args[i]);
    }
    for (int i = 0; i < CONCURRENT_WORKERS; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

    int success_count = 0;
    int conflict_count = 0;

    for (int i = 0; i < CONCURRENT_WORKERS; i++) {
        if (args[i].result_code == ENTITY_ERR_OK) {
            success_count++;
        } else if (args[i].result_code == ENTITY_ERR_CONFLICT) {
            conflict_count++;
        }
    }

    EXPECT_TRUE(success_count == 1, "Exactly one concurrent thread succeeds");
    EXPECT_TRUE(conflict_count == CONCURRENT_WORKERS - 1, "All other concurrent threads receive CONFLICT");

    char count_query[256];
    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM entities WHERE organization_id = %lld AND name = 'Concurrent Corp';",
             org_id);
    db_client_query(&client, count_query);
    MYSQL_RES *res = db_client_store_result(&client);
    MYSQL_ROW row = mysql_fetch_row(res);
    int db_count = row ? atoi(row[0]) : 0;
    db_client_free_result(res);

    EXPECT_TRUE(db_count == 1, "Database contains exactly one record for Concurrent Corp");

    db_client_close(&client);

    printf("\nEntity Concurrency Tests Completed. Passed: %d, Failed: %d\n",
           g_tests_passed, g_tests_failed);

    return g_tests_failed == 0 ? 0 : 1;
}
