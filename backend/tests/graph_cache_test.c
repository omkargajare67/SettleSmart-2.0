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
                         int64_t amount_paise, const char *created_at) {
    memset(tx, 0, sizeof(Transaction));
    tx->id = id;
    tx->organization_id = 1;
    tx->sender_entity_id = sender;
    tx->receiver_entity_id = receiver;
    tx->amount_paise = amount_paise;
    snprintf(tx->amount_str, sizeof(tx->amount_str), "%.2f", (double)amount_paise / 100.0);
    snprintf(tx->currency, sizeof(tx->currency), "INR");
    tx->status = TRANSACTION_STATUS_COMPLETED;
    tx->transaction_type = TRANSACTION_TYPE_STANDARD;
    strncpy(tx->created_at, created_at, sizeof(tx->created_at) - 1);
}

static void test_bounded_capacity_and_fifo_eviction(void) {
    DirectedGraph g;
    graph_init(&g, 4);

    Transaction t1, t2, t3, t4, t5, t6;
    make_test_tx(&t1, 10, 1, 2, 100, "2026-11-16 10:00:00");
    make_test_tx(&t2, 20, 2, 3, 200, "2026-11-16 10:01:00");
    make_test_tx(&t3, 30, 3, 4, 300, "2026-11-16 10:02:00");
    make_test_tx(&t4, 40, 4, 5, 400, "2026-11-16 10:03:00");

    graph_add_edge(&g, &t1);
    graph_add_edge(&g, &t2);
    graph_add_edge(&g, &t3);
    graph_add_edge(&g, &t4);
    EXPECT_TRUE(graph_get_edge_count(&g) == 4, "Edge count filled to capacity 4");

    make_test_tx(&t5, 50, 5, 6, 500, "2026-11-16 10:04:00");
    EXPECT_TRUE(graph_add_edge(&g, &t5) == 0, "Insert 5th edge triggers eviction of oldest (T1)");
    EXPECT_TRUE(graph_get_edge_count(&g) == 4, "Edge count strictly bounded at 4");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 10, NULL) == NULL, "T1 was evicted");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 50, NULL) != NULL, "T5 present");

    make_test_tx(&t6, 60, 6, 7, 600, "2026-11-16 10:05:00");
    EXPECT_TRUE(graph_add_edge(&g, &t6) == 0, "Insert 6th edge triggers eviction of T2");
    EXPECT_TRUE(graph_get_edge_count(&g) == 4, "Edge count remains 4");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 20, NULL) == NULL, "T2 was evicted");

    graph_destroy(&g);
}

static void test_isolated_node_cleanup(void) {
    DirectedGraph g;
    graph_init(&g, 2);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 1, 101, 102, 1000, "2026-11-16 10:00:00");
    make_test_tx(&t2, 2, 201, 202, 2000, "2026-11-16 10:01:00");
    make_test_tx(&t3, 3, 301, 302, 3000, "2026-11-16 10:02:00");

    graph_add_edge(&g, &t1);
    graph_add_edge(&g, &t2);
    EXPECT_TRUE(graph_get_node_count(&g) == 4, "4 nodes for 2 disconnected edges");

    graph_add_edge(&g, &t3);
    EXPECT_TRUE(graph_get_edge_count(&g) == 2, "Edge count bounded at 2");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 1, NULL) == NULL, "Edge 1 was evicted");

    EXPECT_TRUE(graph_find_node(&g, 101) == NULL, "Isolated sender node 101 pruned");
    EXPECT_TRUE(graph_find_node(&g, 102) == NULL, "Isolated receiver node 102 pruned");
    EXPECT_TRUE(graph_get_node_count(&g) == 4, "Node count adjusted to 4 (nodes for edges 2 and 3)");

    graph_destroy(&g);
}

static void test_stress_cache_eviction(void) {
    DirectedGraph g;
    graph_init(&g, 50);

    for (int i = 1; i <= 1000; i++) {
        Transaction tx;
        char time_buf[32];
        snprintf(time_buf, sizeof(time_buf), "2026-11-16 %02d:%02d:%02d",
                 (i / 3600) % 24, (i / 60) % 60, i % 60);
        long long s = (i % 20) + 1;
        long long r = ((i + 1) % 20) + 1;
        if (s == r) r = ((r + 1) % 20) + 1;
        make_test_tx(&tx, i, s, r, i * 10, time_buf);
        graph_add_edge(&g, &tx);
    }

    EXPECT_TRUE(graph_get_edge_count(&g) == 50, "Edge count strictly bounded at 50 after 1000 inserts");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 1, NULL) == NULL, "First transaction evicted");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 1000, NULL) != NULL, "Latest transaction 1000 present");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 951, NULL) != NULL, "Transaction 951 present");

    graph_destroy(&g);
}

int main(void) {
    printf("Starting Graph Cache and Memory Scalability Tests...\n");

    test_bounded_capacity_and_fifo_eviction();
    test_isolated_node_cleanup();
    test_stress_cache_eviction();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
