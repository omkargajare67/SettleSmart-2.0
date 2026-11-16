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

static long long g_idempotency_seq = 2000;
static long long insert_db_tx(DBClient *client, long long org_id, long long sender, long long receiver,
                              const char *amount_str, const char *status, const char *created_at) {
    char query[1024];
    char ik[64];
    char fp[65];
    snprintf(ik, sizeof(ik), "ik_grecov_%lld_%lld", org_id, ++g_idempotency_seq);
    memset(fp, 'b', 64);
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

    long long org_id = create_test_org(&client, "Org_Graph_Recovery_Test");
    EXPECT_TRUE(org_id > 0, "Create test organization for graph recovery");

    cleanup_test_org(&client, org_id);
    org_id = create_test_org(&client, "Org_Graph_Recovery_Test");

    long long e1 = create_test_entity(&client, org_id, "E1_Alpha");
    long long e2 = create_test_entity(&client, org_id, "E2_Beta");
    long long e3 = create_test_entity(&client, org_id, "E3_Gamma");
    EXPECT_TRUE(e1 > 0 && e2 > 0 && e3 > 0, "Create 3 active entities");

    long long t1 = insert_db_tx(&client, org_id, e1, e2, "100.00", "Completed", "2026-03-30 10:00:00");
    long long t2 = insert_db_tx(&client, org_id, e2, e3, "250.50", "Completed", "2026-03-30 10:05:00");
    long long t3 = insert_db_tx(&client, org_id, e1, e2, "50.00", "Pending", "2026-03-30 10:10:00");
    EXPECT_TRUE(t1 > 0 && t2 > 0 && t3 > 0, "Insert 3 transactions in DB");

    DirectedGraph graph;
    graph_init(&graph, 100);

    int res = graph_service_recover_from_db(&graph, &client, org_id, 100);
    EXPECT_TRUE(res == 0, "Graph recovery from DB succeeds");
    EXPECT_TRUE(graph_get_node_count(&graph) == 3, "Recovered graph has 3 nodes");
    EXPECT_TRUE(graph_get_edge_count(&graph) == 3, "Recovered graph has 3 edges");

    GraphNode *n1 = graph_find_node(&graph, e1);
    GraphNode *n2 = graph_find_node(&graph, e2);
    GraphNode *n3 = graph_find_node(&graph, e3);
    EXPECT_TRUE(n1 != NULL && strcmp(n1->entity_name, "E1_Alpha") == 0, "Node 1 label verified");
    EXPECT_TRUE(n2 != NULL && strcmp(n2->entity_name, "E2_Beta") == 0, "Node 2 label verified");
    EXPECT_TRUE(n3 != NULL && strcmp(n3->entity_name, "E3_Gamma") == 0, "Node 3 label verified");

    GraphEdge *found_t1 = graph_find_edge_by_tx_id(&graph, t1, NULL);
    EXPECT_TRUE(found_t1 != NULL && found_t1->amount_paise == 10000, "Edge t1 recovered with exact paise");

    GraphEdge *found_t2 = graph_find_edge_by_tx_id(&graph, t2, NULL);
    EXPECT_TRUE(found_t2 != NULL && found_t2->amount_paise == 25050, "Edge t2 recovered with exact paise");

    GraphEdge *found_t3 = graph_find_edge_by_tx_id(&graph, t3, NULL);
    EXPECT_TRUE(found_t3 != NULL && found_t3->amount_paise == 5000, "Parallel edge t3 recovered");

    graph_destroy(&graph);

    DirectedGraph bounded_graph;
    graph_init(&bounded_graph, 2);
    res = graph_service_recover_from_db(&bounded_graph, &client, org_id, 2);
    EXPECT_TRUE(res == 0, "Bounded recovery succeeds");
    EXPECT_TRUE(graph_get_edge_count(&bounded_graph) == 2, "Edge capacity limit respected during recovery");
    graph_destroy(&bounded_graph);

    cleanup_test_org(&client, org_id);
    db_client_close(&client);

    printf("Graph recovery tests: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
