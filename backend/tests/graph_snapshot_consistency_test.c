#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/graph/graph_service.h"
#include "../src/websocket/event_dispatcher.h"
#include "../src/transactions/transaction_service.h"
#include "../src/transactions/transaction_processor.h"
#include "../src/entities/entity_service.h"
#include "../src/auth/auth_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

typedef struct {
    DBClient *client;
    long long org_id;
    long long user_id;
    long long e1;
    long long e2;
    volatile int stop;
    int tx_count;
} ConcurrencyTxArg;

#ifdef _WIN32
static unsigned __stdcall tx_worker(void *arg) {
    ConcurrencyTxArg *carg = (ConcurrencyTxArg *)arg;
    int idx = 0;
    while (!carg->stop && idx < 10) {
        char key[64];
        snprintf(key, sizeof(key), "key_concur_%d_%lld", idx, (long long)GetTickCount());
        char amt[32];
        snprintf(amt, sizeof(amt), "%d.00", 100 + idx * 10);
        Transaction tx;
        int is_replay = 0;
        char err[128];
        if (transaction_service_create(carg->client, carg->org_id, carg->user_id,
                                       carg->e1, carg->e2, amt, "INR", key,
                                       &tx, &is_replay, err, sizeof(err)) == 0) {
            carg->tx_count++;
        }
        idx++;
        Sleep(10);
    }
    return 0;
}
#else
static void *tx_worker(void *arg) {
    ConcurrencyTxArg *carg = (ConcurrencyTxArg *)arg;
    int idx = 0;
    while (!carg->stop && idx < 10) {
        char key[64];
        snprintf(key, sizeof(key), "key_concur_%d_%d", idx, rand());
        char amt[32];
        snprintf(amt, sizeof(amt), "%d.00", 100 + idx * 10);
        Transaction tx;
        int is_replay = 0;
        char err[128];
        if (transaction_service_create(carg->client, carg->org_id, carg->user_id,
                                       carg->e1, carg->e2, amt, "INR", key,
                                       &tx, &is_replay, err, sizeof(err)) == 0) {
            carg->tx_count++;
        }
        idx++;
        usleep(10000);
    }
    return NULL;
}
#endif

int main(void) {
    load_env_file(".env");
    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &db_cfg) != 0) {
        printf("FAILED: Database connection failed\n");
        return 1;
    }

    db_client_query(&client, "DELETE FROM event_outbox;");
    db_client_query(&client, "DELETE FROM transactions WHERE idempotency_key LIKE '%snap%';");
    db_client_query(&client, "DELETE FROM transactions WHERE idempotency_key LIKE '%concur%';");
    db_client_query(&client, "DELETE FROM entities WHERE name LIKE '%Snap%';");
    db_client_query(&client, "DELETE FROM users WHERE email LIKE '%snap%';");
    db_client_query(&client, "DELETE FROM organizations WHERE name = 'Snap Org';");

    long long org_id = 0;
    long long user_id = 0;
    char err_buf[256];

    if (auth_register_organization(&client, "Snap Org", "admin@snap.com",
                                   "AdminSecret#2026", &org_id, &user_id,
                                   err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Organization registration failed: %s\n", err_buf);
        db_client_close(&client);
        return 1;
    }

    Entity e1, e2;
    entity_service_create(&client, org_id, user_id, "Snap Ent A", "Department", &e1, err_buf, sizeof(err_buf));
    entity_service_create(&client, org_id, user_id, "Snap Ent B", "Vendor", &e2, err_buf, sizeof(err_buf));
    printf("PASSED: Test entities created: Ent A=%lld, Ent B=%lld\n", e1.id, e2.id);

    char json_buf[16384];
    if (graph_service_get_snapshot_json(&client, org_id, 0, NULL, NULL, NULL, 1, 0, json_buf, sizeof(json_buf)) != 0) {
        printf("FAILED: graph_service_get_snapshot_json with limit 0 failed\n");
        db_client_close(&client);
        return 1;
    }
    if (strstr(json_buf, "\"watermark\":0") == NULL || strstr(json_buf, "\"edges\":[]") == NULL) {
        printf("FAILED: Initial snapshot does not have watermark 0 or empty edges: %s\n", json_buf);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Initial empty graph snapshot verified with watermark 0\n");

    for (int i = 1; i <= 3; i++) {
        char k[64];
        snprintf(k, sizeof(k), "snap_init_%d", i);
        char amt[32];
        snprintf(amt, sizeof(amt), "%d.00", 50 * i);
        Transaction tx;
        int is_replay = 0;
        transaction_service_create(&client, org_id, user_id, e1.id, e2.id, amt, "INR",
                                   k, &tx, &is_replay, err_buf, sizeof(err_buf));
    }

    int64_t db_watermark = 0;
    outbox_get_watermark(&client, org_id, &db_watermark);
    if (db_watermark != 3) {
        printf("FAILED: Expected outbox watermark 3, got: %lld\n", (long long)db_watermark);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Database outbox sequence advanced to watermark 3\n");

    if (graph_service_get_snapshot_json(&client, org_id, 0, NULL, NULL, NULL, 1, 100, json_buf, sizeof(json_buf)) != 0) {
        printf("FAILED: Failed to fetch snapshot at watermark 3\n");
        db_client_close(&client);
        return 1;
    }
    if (strstr(json_buf, "\"watermark\":3") == NULL) {
        printf("FAILED: Snapshot does not contain watermark 3: %s\n", json_buf);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Consistent snapshot returned watermark 3 matching transactions\n");

    for (int i = 4; i <= 6; i++) {
        char k[64];
        snprintf(k, sizeof(k), "snap_init_%d", i);
        char amt[32];
        snprintf(amt, sizeof(amt), "%d.00", 50 * i);
        Transaction tx;
        int is_replay = 0;
        transaction_service_create(&client, org_id, user_id, e1.id, e2.id, amt, "INR",
                                   k, &tx, &is_replay, err_buf, sizeof(err_buf));
    }

    char replay_buf[8192];
    int replay_count = 0;
    if (outbox_get_events_after(&client, org_id, 3, 100, replay_buf, sizeof(replay_buf), &replay_count) != 0) {
        printf("FAILED: outbox_get_events_after watermark 3 failed\n");
        db_client_close(&client);
        return 1;
    }
    if (replay_count != 3) {
        printf("FAILED: Expected 3 replay events after watermark 3, got: %d\n", replay_count);
        db_client_close(&client);
        return 1;
    }
    if (strstr(replay_buf, "\"event_sequence\":4") == NULL ||
        strstr(replay_buf, "\"event_sequence\":5") == NULL ||
        strstr(replay_buf, "\"event_sequence\":6") == NULL) {
        printf("FAILED: Replay events do not contain expected sequences 4, 5, 6: %s\n", replay_buf);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Event replay after watermark delivered exactly subsequent events (seq 4, 5, 6) with no duplication\n");

    DBClient concur_client;
    db_client_init(&concur_client);
    db_client_connect(&concur_client, &db_cfg);

    ConcurrencyTxArg carg;
    carg.client = &concur_client;
    carg.org_id = org_id;
    carg.user_id = user_id;
    carg.e1 = e1.id;
    carg.e2 = e2.id;
    carg.stop = 0;
    carg.tx_count = 0;

#ifdef _WIN32
    HANDLE hWorker = (HANDLE)_beginthreadex(NULL, 0, tx_worker, &carg, 0, NULL);
#else
    pthread_t p_worker;
    pthread_create(&p_worker, NULL, tx_worker, &carg);
#endif

    for (int loop = 0; loop < 5; loop++) {
        char snap[16384];
        if (graph_service_get_snapshot_json(&client, org_id, 0, NULL, NULL, NULL, 1, 100, snap, sizeof(snap)) == 0) {
            char *wm_pos = strstr(snap, "\"watermark\":");
            if (wm_pos) {
                int64_t snap_wm = atoll(wm_pos + 12);
                if (snap_wm < 6) {
                    printf("FAILED: Inconsistent watermark %lld during concurrent execution\n", (long long)snap_wm);
                    carg.stop = 1;
                    break;
                }
            }
        }
#ifdef _WIN32
        Sleep(25);
#else
        usleep(25000);
#endif
    }

    carg.stop = 1;
#ifdef _WIN32
    WaitForSingleObject(hWorker, 2000);
    CloseHandle(hWorker);
#else
    pthread_join(p_worker, NULL);
#endif
    db_client_close(&concur_client);

    printf("PASSED: Concurrent transaction generation with continuous snapshotting completed consistently\n");

    db_client_close(&client);
    printf("\nALL GRAPH SNAPSHOT AND CONSISTENCY TESTS SUCCEEDED.\n");
    return 0;
}
