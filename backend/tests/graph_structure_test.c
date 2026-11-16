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
                         int64_t amount_paise, TransactionStatus status, const char *created_at) {
    memset(tx, 0, sizeof(Transaction));
    tx->id = id;
    tx->organization_id = 1;
    tx->sender_entity_id = sender;
    tx->receiver_entity_id = receiver;
    tx->amount_paise = amount_paise;
    snprintf(tx->amount_str, sizeof(tx->amount_str), "%.2f", (double)amount_paise / 100.0);
    snprintf(tx->currency, sizeof(tx->currency), "INR");
    tx->status = status;
    tx->transaction_type = TRANSACTION_TYPE_STANDARD;
    strncpy(tx->created_at, created_at, sizeof(tx->created_at) - 1);
}

static void test_a_node_creation(void) {
    DirectedGraph g;
    EXPECT_TRUE(graph_init(&g, 500) == 0, "Graph init returns 0");
    EXPECT_TRUE(graph_get_node_count(&g) == 0, "Initial node count is 0");
    EXPECT_TRUE(graph_get_edge_count(&g) == 0, "Initial edge count is 0");

    GraphNode *na = graph_get_or_create_node(&g, 101, "Entity A", "Department");
    GraphNode *nb = graph_get_or_create_node(&g, 102, "Entity B", "Vendor");
    GraphNode *nc = graph_get_or_create_node(&g, 103, "Entity C", "Client");

    EXPECT_TRUE(na != NULL && nb != NULL && nc != NULL, "Created nodes A, B, C");
    EXPECT_TRUE(graph_get_node_count(&g) == 3, "Node count is 3");

    GraphNode *na_dup = graph_get_or_create_node(&g, 101, "Entity A", "Department");
    EXPECT_TRUE(na_dup == na, "Duplicate node lookup returns existing node pointer");
    EXPECT_TRUE(graph_get_node_count(&g) == 3, "Node count remains 3 after duplicate lookup");

    GraphNode *found_b = graph_find_node(&g, 102);
    EXPECT_TRUE(found_b != NULL && strcmp(found_b->entity_name, "Entity B") == 0, "Find node 102 returns Entity B");
    EXPECT_TRUE(graph_find_node(&g, 999) == NULL, "Find nonexistent node returns NULL");

    graph_destroy(&g);
}

static void test_b_edge_creation(void) {
    DirectedGraph g;
    graph_init(&g, 500);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 1, 101, 102, 50000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:00:00");
    make_test_tx(&t2, 2, 102, 103, 30000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:01:00");
    make_test_tx(&t3, 3, 101, 103, 20000, TRANSACTION_STATUS_PENDING, "2026-11-16 10:02:00");

    EXPECT_TRUE(graph_add_edge(&g, &t1) == 0, "Add edge T1 (A -> B: 500.00)");
    EXPECT_TRUE(graph_add_edge(&g, &t2) == 0, "Add edge T2 (B -> C: 300.00)");
    EXPECT_TRUE(graph_add_edge(&g, &t3) == 0, "Add edge T3 (A -> C: 200.00)");

    EXPECT_TRUE(graph_get_node_count(&g) == 3, "Node count is 3");
    EXPECT_TRUE(graph_get_edge_count(&g) == 3, "Edge count is 3");

    graph_destroy(&g);
}

static void test_c_direction_preservation(void) {
    DirectedGraph g;
    graph_init(&g, 500);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 1, 101, 102, 50000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:00:00");
    make_test_tx(&t2, 2, 102, 103, 30000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:01:00");
    make_test_tx(&t3, 3, 101, 103, 20000, TRANSACTION_STATUS_PENDING, "2026-11-16 10:02:00");

    graph_add_edge(&g, &t1);
    graph_add_edge(&g, &t2);
    graph_add_edge(&g, &t3);

    GraphNode *node_a = graph_find_node(&g, 101);
    EXPECT_TRUE(node_a != NULL, "Node A found");

    int a_to_b = 0;
    int a_to_c = 0;
    GraphEdge *e = node_a->edges;
    while (e) {
        if (e->receiver_entity_id == 102) a_to_b++;
        if (e->receiver_entity_id == 103) a_to_c++;
        e = e->next;
    }
    EXPECT_TRUE(a_to_b == 1, "Direct edge A -> B exists exactly once");
    EXPECT_TRUE(a_to_c == 1, "Direct edge A -> C exists exactly once");

    GraphNode *node_c = graph_find_node(&g, 103);
    EXPECT_TRUE(node_c != NULL && node_c->edges == NULL, "Node C has zero outgoing edges (reverse does not exist)");

    GraphNode *node_b = graph_find_node(&g, 102);
    int b_to_a = 0;
    e = node_b->edges;
    while (e) {
        if (e->receiver_entity_id == 101) b_to_a++;
        e = e->next;
    }
    EXPECT_TRUE(b_to_a == 0, "Reverse edge B -> A does not exist");

    graph_destroy(&g);
}

static void test_d_multiple_transfers(void) {
    DirectedGraph g;
    graph_init(&g, 500);

    Transaction t1, t4;
    make_test_tx(&t1, 101, 10, 20, 50000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:00:00");
    make_test_tx(&t4, 102, 10, 20, 20000, TRANSACTION_STATUS_PENDING, "2026-11-16 10:05:00");

    EXPECT_TRUE(graph_add_edge(&g, &t1) == 0, "Insert transfer 101: 10 -> 20 (500.00)");
    EXPECT_TRUE(graph_add_edge(&g, &t4) == 0, "Insert transfer 102: 10 -> 20 (200.00)");
    EXPECT_TRUE(graph_get_edge_count(&g) == 2, "Both transfers preserved as distinct edges");

    GraphNode *node = graph_find_node(&g, 10);
    int count = 0;
    int64_t sum = 0;
    GraphEdge *e = node->edges;
    while (e) {
        if (e->receiver_entity_id == 20) {
            count++;
            sum += e->amount_paise;
        }
        e = e->next;
    }
    EXPECT_TRUE(count == 2, "Exactly 2 distinct outgoing edges from 10 to 20");
    EXPECT_TRUE(sum == 70000, "Sum of distinct edges matches exact combined paise (70000)");

    graph_destroy(&g);
}

static void test_e_transaction_lookup(void) {
    DirectedGraph g;
    graph_init(&g, 500);

    Transaction t1, t2;
    make_test_tx(&t1, 5001, 1, 2, 50000, TRANSACTION_STATUS_PENDING, "2026-11-16 10:00:00");
    make_test_tx(&t2, 5002, 2, 3, 30000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:01:00");

    graph_add_edge(&g, &t1);
    graph_add_edge(&g, &t2);

    long long sender_id = 0;
    GraphEdge *found1 = graph_find_edge_by_tx_id(&g, 5001, &sender_id);
    EXPECT_TRUE(found1 != NULL && found1->transaction_id == 5001 && sender_id == 1, "Lookup tx 5001 returns correct edge and sender");
    EXPECT_TRUE(found1 != NULL && found1->amount_paise == 50000, "Found tx 5001 has correct amount paise");

    GraphEdge *found2 = graph_find_edge_by_tx_id(&g, 5002, &sender_id);
    EXPECT_TRUE(found2 != NULL && found2->transaction_id == 5002 && sender_id == 2, "Lookup tx 5002 returns correct edge and sender");

    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 9999, NULL) == NULL, "Lookup nonexistent tx returns NULL");

    graph_destroy(&g);
}

static void test_f_status_update(void) {
    DirectedGraph g;
    graph_init(&g, 500);

    Transaction t1;
    make_test_tx(&t1, 7001, 10, 20, 50000, TRANSACTION_STATUS_PENDING, "2026-11-16 10:00:00");
    graph_add_edge(&g, &t1);

    EXPECT_TRUE(graph_update_edge_status(&g, 7001, TRANSACTION_STATUS_COMPLETED) == 0, "Update status Pending -> Completed returns 0");

    GraphEdge *e = graph_find_edge_by_tx_id(&g, 7001, NULL);
    EXPECT_TRUE(e != NULL && e->status == TRANSACTION_STATUS_COMPLETED, "Edge status updated to Completed");
    EXPECT_TRUE(e != NULL && e->amount_paise == 50000, "Edge amount preserved after status update");
    EXPECT_TRUE(e != NULL && e->sender_entity_id == 10 && e->receiver_entity_id == 20, "Edge endpoints preserved");

    EXPECT_TRUE(graph_update_edge_status(&g, 7001, TRANSACTION_STATUS_REVERSED) == 0, "Update status Completed -> Reversed returns 0");
    e = graph_find_edge_by_tx_id(&g, 7001, NULL);
    EXPECT_TRUE(e != NULL && e->status == TRANSACTION_STATUS_REVERSED, "Edge status updated to Reversed");

    EXPECT_TRUE(graph_update_edge_status(&g, 9999, TRANSACTION_STATUS_COMPLETED) == -1, "Update nonexistent edge returns -1");

    graph_destroy(&g);
}

static void test_g_bounded_cache_eviction(void) {
    DirectedGraph g;
    graph_init(&g, 3);

    Transaction t1, t2, t3, t4;
    make_test_tx(&t1, 1, 10, 20, 1000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:00:00");
    make_test_tx(&t2, 2, 20, 30, 2000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:01:00");
    make_test_tx(&t3, 3, 30, 40, 3000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:02:00");
    make_test_tx(&t4, 4, 40, 50, 4000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:03:00");

    graph_add_edge(&g, &t1);
    graph_add_edge(&g, &t2);
    graph_add_edge(&g, &t3);
    EXPECT_TRUE(graph_get_edge_count(&g) == 3, "Edge count at capacity is 3");

    EXPECT_TRUE(graph_add_edge(&g, &t4) == 0, "Insert 4th edge beyond capacity triggers eviction");
    EXPECT_TRUE(graph_get_edge_count(&g) == 3, "Edge count remains bounded at 3");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 1, NULL) == NULL, "Oldest edge (T1) was evicted");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 2, NULL) != NULL, "T2 retained");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 3, NULL) != NULL, "T3 retained");
    EXPECT_TRUE(graph_find_edge_by_tx_id(&g, 4, NULL) != NULL, "T4 retained");

    graph_destroy(&g);
}

static void test_h_memory_management(void) {
    DirectedGraph g;
    graph_init(&g, 10);

    for (int i = 1; i <= 5; i++) {
        Transaction tx;
        make_test_tx(&tx, i, i, i + 1, i * 1000, TRANSACTION_STATUS_COMPLETED, "2026-11-16 10:00:00");
        graph_add_edge(&g, &tx);
    }
    EXPECT_TRUE(graph_get_edge_count(&g) == 5, "5 edges before clear");
    EXPECT_TRUE(graph_get_node_count(&g) == 6, "6 nodes before clear");

    graph_clear(&g);
    EXPECT_TRUE(graph_get_edge_count(&g) == 0, "0 edges after clear");
    EXPECT_TRUE(graph_get_node_count(&g) == 0, "0 nodes after clear");
    EXPECT_TRUE(g.nodes == NULL, "Nodes pointer NULL after clear");

    Transaction tx;
    make_test_tx(&tx, 100, 10, 20, 5000, TRANSACTION_STATUS_PENDING, "2026-11-16 11:00:00");
    EXPECT_TRUE(graph_add_edge(&g, &tx) == 0, "Insert works after clear");
    EXPECT_TRUE(graph_get_edge_count(&g) == 1, "Edge count is 1 after insert");

    graph_destroy(&g);
    EXPECT_TRUE(graph_get_edge_count(&g) == 0 && g.max_edge_capacity == 0, "Destroyed graph has 0 capacity");
}

int main(void) {
    printf("Starting Directed Weighted Graph Structure Tests (Tests A-H)...\n");

    test_a_node_creation();
    test_b_edge_creation();
    test_c_direction_preservation();
    test_d_multiple_transfers();
    test_e_transaction_lookup();
    test_f_status_update();
    test_g_bounded_cache_eviction();
    test_h_memory_management();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
