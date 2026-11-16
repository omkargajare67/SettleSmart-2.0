#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/dsa/dsa_state.h"
#include "../src/graph/graph.h"
#include "../src/websocket/outbox_consumer.h"
#include "../src/transactions/transaction_service.h"
#include "../src/transactions/transaction_processor.h"
#include "../src/entities/entity_service.h"
#include "../src/auth/auth_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    load_env_file(".env");
    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client_a;
    DBClient client_b;

    if (db_client_init(&client_a) != 0 || db_client_connect(&client_a, &db_cfg) != 0) {
        printf("FAILED: client_a connection failed\n");
        return 1;
    }
    if (db_client_init(&client_b) != 0 || db_client_connect(&client_b, &db_cfg) != 0) {
        printf("FAILED: client_b connection failed\n");
        db_client_close(&client_a);
        return 1;
    }

    db_client_query(&client_a, "SET FOREIGN_KEY_CHECKS=0;");
    db_client_query(&client_a, "DELETE FROM outbox_consumer_cursors;");
    db_client_query(&client_a, "DELETE FROM event_outbox;");
    db_client_query(&client_a, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'MultiDsa Org');");
    db_client_query(&client_a, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'MultiDsa Org');");
    db_client_query(&client_a, "DELETE FROM sessions WHERE user_id IN (SELECT id FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'MultiDsa Org'));");
    db_client_query(&client_a, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name = 'MultiDsa Org');");
    db_client_query(&client_a, "DELETE FROM organizations WHERE name = 'MultiDsa Org';");
    db_client_query(&client_a, "SET FOREIGN_KEY_CHECKS=1;");

    long long org_id = 0;
    long long user_id = 0;
    char err_buf[256];

    if (auth_register_organization(&client_a, "MultiDsa Org", "admin@multidsa.com",
                                   "AdminSecret#2026", &org_id, &user_id,
                                   err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Organization registration failed: %s\n", err_buf);
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }

    Entity e1, e2;
    entity_service_create(&client_a, org_id, user_id, "MultiDsa Ent 1", ENTITY_TYPE_DEPARTMENT, &e1, err_buf, sizeof(err_buf));
    entity_service_create(&client_a, org_id, user_id, "MultiDsa Ent 2", ENTITY_TYPE_VENDOR, &e2, err_buf, sizeof(err_buf));

    DsaState state_a;
    DirectedGraph graph_a;
    dsa_state_init(&state_a, org_id);
    graph_init(&graph_a, 100);
    hashmap_insert(&state_a.entity_balances, e1.id, 0);
    hashmap_insert(&state_a.entity_balances, e2.id, 0);

    DsaState state_b;
    DirectedGraph graph_b;
    dsa_state_init(&state_b, org_id);
    graph_init(&graph_b, 100);
    hashmap_insert(&state_b.entity_balances, e1.id, 0);
    hashmap_insert(&state_b.entity_balances, e2.id, 0);

    OutboxConsumer consumer_a;
    outbox_consumer_init(&consumer_a, "inst_dsa_A", &client_a, &state_a, &graph_a);

    OutboxConsumer consumer_b;
    outbox_consumer_init(&consumer_b, "inst_dsa_B", &client_b, &state_b, &graph_b);

    printf("PASSED: Independent DsaState, DirectedGraph and OutboxConsumers initialized for Instance A and B\n");

    Transaction tx1;
    int is_replay = 0;
    if (transaction_service_create(&client_a, org_id, user_id, e1.id, e2.id,
                                   "1000.00", "INR", "a0000000-0000-4000-8000-000000000001",
                                   &tx1, &is_replay, err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Transaction creation on Instance A failed: %s\n", err_buf);
        return 1;
    }

    Transaction processed_tx;
    if (transaction_processor_process_tx(&client_a, &state_a, &graph_a, org_id, tx1.id,
                                         &processed_tx, err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Transaction processing on Instance A failed: %s\n", err_buf);
        return 1;
    }
    printf("PASSED: Transaction processed to Completed on Instance A\n");

    int64_t b1_a = 0, b2_a = 0;
    hashmap_get_balance(&state_a.entity_balances, e1.id, &b1_a);
    hashmap_get_balance(&state_a.entity_balances, e2.id, &b2_a);
    if (b1_a != -100000 || b2_a != 100000) {
        printf("FAILED: Instance A balances incorrect: e1=%lld, e2=%lld\n", (long long)b1_a, (long long)b2_a);
        return 1;
    }
    printf("PASSED: Instance A balances updated immediately: e1=-1000.00, e2=+1000.00\n");

    int64_t b1_b = 0, b2_b = 0;
    hashmap_get_balance(&state_b.entity_balances, e1.id, &b1_b);
    hashmap_get_balance(&state_b.entity_balances, e2.id, &b2_b);
    if (b1_b != 0 || b2_b != 0) {
        printf("FAILED: Instance B prematurely updated before outbox poll\n");
        return 1;
    }
    printf("PASSED: Instance B balances remained untouched before outbox poll\n");

    int events_b = 0;
    outbox_consumer_poll_org(&consumer_b, org_id, 100, &events_b);
    if (events_b < 2) {
        printf("FAILED: Expected at least 2 events for Instance B, got: %d\n", events_b);
        return 1;
    }

    hashmap_get_balance(&state_b.entity_balances, e1.id, &b1_b);
    hashmap_get_balance(&state_b.entity_balances, e2.id, &b2_b);
    if (b1_b != -100000 || b2_b != 100000) {
        printf("FAILED: Instance B balances did not sync after poll: e1=%lld, e2=%lld\n", (long long)b1_b, (long long)b2_b);
        return 1;
    }
    printf("PASSED: Instance B balances synchronized via Outbox Consumer: e1=-1000.00, e2=+1000.00\n");

    GraphEdge *e_b = graph_find_edge_by_tx_id(&graph_b, tx1.id, NULL);
    if (!e_b || e_b->status != TRANSACTION_STATUS_COMPLETED) {
        printf("FAILED: Instance B DirectedGraph missing edge or incorrect status\n");
        return 1;
    }
    printf("PASSED: Instance B DirectedGraph synchronized edge with status Completed\n");

    int events_b_repeat = 0;
    outbox_consumer_poll_org(&consumer_b, org_id, 100, &events_b_repeat);
    if (events_b_repeat != 0) {
        printf("FAILED: Repeated poll processed non-zero events: %d\n", events_b_repeat);
        return 1;
    }
    hashmap_get_balance(&state_b.entity_balances, e1.id, &b1_b);
    hashmap_get_balance(&state_b.entity_balances, e2.id, &b2_b);
    if (b1_b != -100000 || b2_b != 100000) {
        printf("FAILED: Balances mutated on repeat poll (idempotency violation)\n");
        return 1;
    }
    printf("PASSED: Outbox consumption is idempotent; repeat poll does not duplicate financial effects\n");

    Transaction tx_rev;
    int is_rev_replay = 0;
    if (transaction_service_create_reversal(&client_b, org_id, user_id, tx1.id,
                                            "b0000000-0000-4000-8000-000000000001",
                                            &tx_rev, &is_rev_replay,
                                            err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Reversal creation on Instance B failed: %s\n", err_buf);
        return 1;
    }

    Transaction processed_rev;
    if (transaction_processor_process_tx(&client_b, &state_b, &graph_b, org_id, tx_rev.id,
                                         &processed_rev, err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Reversal processing on Instance B failed: %s\n", err_buf);
        return 1;
    }
    printf("PASSED: Reversal processed on Instance B\n");

    hashmap_get_balance(&state_b.entity_balances, e1.id, &b1_b);
    hashmap_get_balance(&state_b.entity_balances, e2.id, &b2_b);
    if (b1_b != 0 || b2_b != 0) {
        printf("FAILED: Instance B balances not neutralized after reversal: e1=%lld, e2=%lld\n",
               (long long)b1_b, (long long)b2_b);
        return 1;
    }
    printf("PASSED: Instance B balances neutralized to 0 after reversal\n");

    int events_a = 0;
    outbox_consumer_poll_org(&consumer_a, org_id, 100, &events_a);
    hashmap_get_balance(&state_a.entity_balances, e1.id, &b1_a);
    hashmap_get_balance(&state_a.entity_balances, e2.id, &b2_a);
    if (b1_a != 0 || b2_a != 0) {
        printf("FAILED: Instance A balances not neutralized after syncing reversal: e1=%lld, e2=%lld\n",
               (long long)b1_a, (long long)b2_a);
        return 1;
    }
    printf("PASSED: Instance A balances neutralized to 0 after syncing reversal from Instance B\n");

    dsa_state_destroy(&state_a);
    dsa_state_destroy(&state_b);
    graph_destroy(&graph_a);
    graph_destroy(&graph_b);
    outbox_consumer_destroy(&consumer_a);
    outbox_consumer_destroy(&consumer_b);

    db_client_close(&client_a);
    db_client_close(&client_b);
    printf("\nALL MULTI-INSTANCE DSA AND GRAPH SYNCHRONIZATION TESTS SUCCEEDED.\n");
    return 0;
}

