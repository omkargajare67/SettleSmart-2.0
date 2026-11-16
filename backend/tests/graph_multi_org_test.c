#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/graph/graph.h"
#include "../src/graph/graph_service.h"
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

static long long g_idempotency_seq = 3000;
static long long insert_db_tx(DBClient *client, long long org_id, long long sender, long long receiver,
                              const char *amount_str, const char *status, const char *created_at) {
    char query[1024];
    char ik[64];
    char fp[65];
    snprintf(ik, sizeof(ik), "ik_gmorg_%lld_%lld", org_id, ++g_idempotency_seq);
    memset(fp, 'c', 64);
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

    long long org_a = create_test_org(&client, "Org_Graph_Multi_A");
    long long org_b = create_test_org(&client, "Org_Graph_Multi_B");

    cleanup_test_org(&client, org_a);
    cleanup_test_org(&client, org_b);

    org_a = create_test_org(&client, "Org_Graph_Multi_A");
    org_b = create_test_org(&client, "Org_Graph_Multi_B");

    long long ea1 = create_test_entity(&client, org_a, "EA1_Entity");
    long long ea2 = create_test_entity(&client, org_a, "EA2_Entity");

    long long eb1 = create_test_entity(&client, org_b, "EB1_Entity");
    long long eb2 = create_test_entity(&client, org_b, "EB2_Entity");

    long long ta1 = insert_db_tx(&client, org_a, ea1, ea2, "100.00", "Completed", "2026-03-30 11:00:00");
    long long tb1 = insert_db_tx(&client, org_b, eb1, eb2, "200.00", "Completed", "2026-03-30 11:05:00");

    DirectedGraph graph_a;
    graph_init(&graph_a, 100);
    int res_a = graph_service_recover_from_db(&graph_a, &client, org_a, 100);
    EXPECT_TRUE(res_a == 0, "Recovery for Org A succeeds");
    EXPECT_TRUE(graph_get_node_count(&graph_a) == 2, "Graph A has 2 nodes");
    EXPECT_TRUE(graph_get_edge_count(&graph_a) == 1, "Graph A has 1 edge");
    EXPECT_TRUE(graph_find_node(&graph_a, ea1) != NULL, "Graph A contains EA1");
    EXPECT_TRUE(graph_find_node(&graph_a, ea2) != NULL, "Graph A contains EA2");
    EXPECT_TRUE(graph_find_node(&graph_a, eb1) == NULL, "Graph A isolates EB1");
    EXPECT_TRUE(graph_find_node(&graph_a, eb2) == NULL, "Graph A isolates EB2");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&graph_a, ta1, NULL) != NULL, "Graph A contains TA1");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&graph_a, tb1, NULL) == NULL, "Graph A isolates TB1");

    DirectedGraph graph_b;
    graph_init(&graph_b, 100);
    int res_b = graph_service_recover_from_db(&graph_b, &client, org_b, 100);
    EXPECT_TRUE(res_b == 0, "Recovery for Org B succeeds");
    EXPECT_TRUE(graph_get_node_count(&graph_b) == 2, "Graph B has 2 nodes");
    EXPECT_TRUE(graph_get_edge_count(&graph_b) == 1, "Graph B has 1 edge");
    EXPECT_TRUE(graph_find_node(&graph_b, eb1) != NULL, "Graph B contains EB1");
    EXPECT_TRUE(graph_find_node(&graph_b, eb2) != NULL, "Graph B contains EB2");
    EXPECT_TRUE(graph_find_node(&graph_b, ea1) == NULL, "Graph B isolates EA1");
    EXPECT_TRUE(graph_find_node(&graph_b, ea2) == NULL, "Graph B isolates EA2");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&graph_b, tb1, NULL) != NULL, "Graph B contains TB1");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&graph_b, ta1, NULL) == NULL, "Graph B isolates TA1");

    graph_destroy(&graph_a);
    graph_destroy(&graph_b);

    cleanup_test_org(&client, org_a);
    cleanup_test_org(&client, org_b);
    db_client_close(&client);

    printf("Graph multi-org tests: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
