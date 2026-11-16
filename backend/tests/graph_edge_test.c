#include "../src/graph/graph.h"
#include <stdio.h>
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

static void make_test_tx(Transaction *tx, long long id, long long sender, long long receiver,
                         int64_t amount_paise, TransactionStatus status, TransactionType type,
                         long long reversal_of_id, const char *created_at) {
    memset(tx, 0, sizeof(Transaction));
    tx->id = id;
    tx->organization_id = 1;
    tx->sender_entity_id = sender;
    tx->receiver_entity_id = receiver;
    tx->amount_paise = amount_paise;
    snprintf(tx->amount_str, sizeof(tx->amount_str), "%.2f", (double)amount_paise / 100.0);
    snprintf(tx->currency, sizeof(tx->currency), "INR");
    tx->status = status;
    tx->transaction_type = type;
    tx->reversal_of_id = reversal_of_id;
    strncpy(tx->created_at, created_at, sizeof(tx->created_at) - 1);
}

static void test_exact_edge_weights(void) {
    DirectedGraph g;
    graph_init(&g, 100);

    Transaction tx;
    make_test_tx(&tx, 1001, 50, 60, 50025, TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_STANDARD, 0, "2026-11-16 10:00:00");
    EXPECT_TRUE(graph_add_edge(&g, &tx) == 0, "Insert edge with exact paise 50025 (500.25)");

    GraphEdge *e = graph_find_edge_by_tx_id(&g, 1001, NULL);
    EXPECT_TRUE(e != NULL && e->amount_paise == 50025, "Paise amount is exactly 50025");
    EXPECT_TRUE(e != NULL && strcmp(e->amount_str, "500.25") == 0, "Amount string matches exact decimal 500.25");
    EXPECT_TRUE(e != NULL && strcmp(e->currency, "INR") == 0, "Currency is INR");

    graph_destroy(&g);
}

static void test_parallel_edges(void) {
    DirectedGraph g;
    graph_init(&g, 100);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 101, 1, 2, 10000, TRANSACTION_STATUS_PENDING, TRANSACTION_TYPE_STANDARD, 0, "2026-11-16 10:00:00");
    make_test_tx(&t2, 102, 1, 2, 25000, TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_STANDARD, 0, "2026-11-16 10:01:00");
    make_test_tx(&t3, 103, 1, 2, 75000, TRANSACTION_STATUS_DISPUTED, TRANSACTION_TYPE_STANDARD, 0, "2026-11-16 10:02:00");

    graph_add_edge(&g, &t1);
    graph_add_edge(&g, &t2);
    graph_add_edge(&g, &t3);

    EXPECT_TRUE(graph_get_edge_count(&g) == 3, "All 3 parallel edges are distinct");

    GraphEdge *e1 = graph_find_edge_by_tx_id(&g, 101, NULL);
    GraphEdge *e2 = graph_find_edge_by_tx_id(&g, 102, NULL);
    GraphEdge *e3 = graph_find_edge_by_tx_id(&g, 103, NULL);

    EXPECT_TRUE(e1 != NULL && e1->status == TRANSACTION_STATUS_PENDING && e1->amount_paise == 10000, "e1 intact");
    EXPECT_TRUE(e2 != NULL && e2->status == TRANSACTION_STATUS_COMPLETED && e2->amount_paise == 25000, "e2 intact");
    EXPECT_TRUE(e3 != NULL && e3->status == TRANSACTION_STATUS_DISPUTED && e3->amount_paise == 75000, "e3 intact");

    graph_destroy(&g);
}

static void test_reversal_edge_representation(void) {
    DirectedGraph g;
    graph_init(&g, 100);

    Transaction orig, rev;
    make_test_tx(&orig, 201, 10, 20, 50000, TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_STANDARD, 0, "2026-11-16 10:00:00");
    make_test_tx(&rev, 202, 20, 10, 50000, TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_REVERSAL, 201, "2026-11-16 10:05:00");

    EXPECT_TRUE(graph_add_edge(&g, &orig) == 0, "Insert original transaction A -> B (500.00)");
    EXPECT_TRUE(graph_add_edge(&g, &rev) == 0, "Insert reversal transaction B -> A (500.00)");

    EXPECT_TRUE(graph_get_edge_count(&g) == 2, "Both edges exist simultaneously");

    GraphNode *node_a = graph_find_node(&g, 10);
    GraphNode *node_b = graph_find_node(&g, 20);

    EXPECT_TRUE(node_a->edges != NULL && node_a->edges->receiver_entity_id == 20, "Node A has outgoing edge to B");
    EXPECT_TRUE(node_b->edges != NULL && node_b->edges->receiver_entity_id == 10, "Node B has outgoing edge to A (reversal)");

    graph_update_edge_status(&g, 201, TRANSACTION_STATUS_REVERSED);
    GraphEdge *e_orig = graph_find_edge_by_tx_id(&g, 201, NULL);
    GraphEdge *e_rev = graph_find_edge_by_tx_id(&g, 202, NULL);

    EXPECT_TRUE(e_orig->status == TRANSACTION_STATUS_REVERSED, "Original edge marked Reversed");
    EXPECT_TRUE(e_rev->status == TRANSACTION_STATUS_COMPLETED, "Reversal edge remains Completed");
    EXPECT_TRUE(e_orig->amount_paise == 50000 && e_rev->amount_paise == 50000, "Both edge amounts preserved");

    graph_destroy(&g);
}

static void test_edge_removal(void) {
    DirectedGraph g;
    graph_init(&g, 100);

    Transaction t1, t2;
    make_test_tx(&t1, 1, 100, 200, 1000, TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_STANDARD, 0, "2026-11-16 10:00:00");
    make_test_tx(&t2, 2, 100, 300, 2000, TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_STANDARD, 0, "2026-11-16 10:01:00");

    graph_add_edge(&g, &t1);
    graph_add_edge(&g, &t2);
    EXPECT_TRUE(graph_get_edge_count(&g) == 2, "Edge count is 2");

    EXPECT_TRUE(graph_remove_edge_by_tx_id(&g, 999) == -1, "Remove nonexistent edge returns -1");
    EXPECT_TRUE(graph_remove_edge_by_tx_id(&g, 1) == 0, "Remove edge 1 returns 0");
    EXPECT_TRUE(graph_get_edge_count(&g) == 1, "Edge count is 1 after removal");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 1, NULL) == NULL, "Edge 1 is gone");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 2, NULL) != NULL, "Edge 2 is still present");

    EXPECT_TRUE(graph_find_node(&g, 200) == NULL, "Isolated node 200 was cleaned up");
    EXPECT_TRUE(graph_find_node(&g, 100) != NULL, "Node 100 retained because it still has edge 2");

    graph_destroy(&g);
}

int main(void) {
    printf("Starting Graph Edge and Reversal Representation Tests...\n");

    test_exact_edge_weights();
    test_parallel_edges();
    test_reversal_edge_representation();
    test_edge_removal();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
