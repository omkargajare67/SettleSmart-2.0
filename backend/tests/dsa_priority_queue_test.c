#include "../src/dsa/priority_queue.h"
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

static void make_test_tx(Transaction *tx, long long id, int64_t amount_paise, const char *created_at) {
    memset(tx, 0, sizeof(Transaction));
    tx->id = id;
    tx->organization_id = 1;
    tx->sender_entity_id = 10;
    tx->receiver_entity_id = 20;
    tx->amount_paise = amount_paise;
    snprintf(tx->amount_str, sizeof(tx->amount_str), "%.2f", (double)amount_paise / 100.0);
    snprintf(tx->currency, sizeof(tx->currency), "INR");
    tx->status = TRANSACTION_STATUS_PENDING;
    tx->transaction_type = TRANSACTION_TYPE_STANDARD;
    strncpy(tx->created_at, created_at, sizeof(tx->created_at) - 1);
}

static void test_init_and_destroy(void) {
    PriorityQueue pq;
    EXPECT_TRUE(priority_queue_init(&pq, 16) == 0, "PriorityQueue init returns 0");
    EXPECT_TRUE(priority_queue_is_empty(&pq) == 1, "PQ is empty initially");
    EXPECT_TRUE(priority_queue_get_size(&pq) == 0, "PQ size is 0 initially");
    EXPECT_TRUE(pq.capacity == 16, "Capacity is 16");
    priority_queue_destroy(&pq);
    EXPECT_TRUE(pq.items == NULL && pq.size == 0, "Items NULL and size 0 after destroy");
}

static void test_max_heap_order(void) {
    PriorityQueue pq;
    priority_queue_init(&pq, 16);

    Transaction t1, t2, t3, t4, t5;
    make_test_tx(&t1, 1, 50000, "2026-11-15 10:00:00");
    make_test_tx(&t2, 2, 1000000, "2026-11-15 10:01:00");
    make_test_tx(&t3, 3, 250000, "2026-11-15 10:02:00");
    make_test_tx(&t4, 4, 7500000, "2026-11-15 10:03:00");
    make_test_tx(&t5, 5, 10000, "2026-11-15 10:04:00");

    priority_queue_insert(&pq, &t1);
    priority_queue_insert(&pq, &t2);
    priority_queue_insert(&pq, &t3);
    priority_queue_insert(&pq, &t4);
    priority_queue_insert(&pq, &t5);
    EXPECT_TRUE(priority_queue_get_size(&pq) == 5, "Size is 5");

    Transaction out;
    EXPECT_TRUE(priority_queue_peek(&pq, &out) == 0 && out.id == 4, "Peek returns max amount item (t4)");
    EXPECT_TRUE(priority_queue_get_size(&pq) == 5, "Size unchanged after peek");

    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.amount_paise == 7500000, "Extract 1st: 7500000 (t4)");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.amount_paise == 1000000, "Extract 2nd: 1000000 (t2)");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.amount_paise == 250000, "Extract 3rd: 250000 (t3)");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.amount_paise == 50000, "Extract 4th: 50000 (t1)");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.amount_paise == 10000, "Extract 5th: 10000 (t5)");
    EXPECT_TRUE(priority_queue_is_empty(&pq) == 1, "PQ is empty");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == -1, "Extract from empty returns -1");

    priority_queue_destroy(&pq);
}

static void test_deterministic_tie_breaking(void) {
    PriorityQueue pq;
    priority_queue_init(&pq, 16);

    Transaction t1, t2, t3, t4;
    make_test_tx(&t1, 10, 50000, "2026-11-15 10:05:00");
    make_test_tx(&t2, 20, 50000, "2026-11-15 10:01:00");
    make_test_tx(&t3, 30, 50000, "2026-11-15 10:01:00");
    make_test_tx(&t4, 5, 50000, "2026-11-15 10:01:00");

    priority_queue_insert(&pq, &t1);
    priority_queue_insert(&pq, &t2);
    priority_queue_insert(&pq, &t3);
    priority_queue_insert(&pq, &t4);

    Transaction out;
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.id == 5, "Tie-break 1st: same earliest time, lowest ID (5)");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.id == 20, "Tie-break 2nd: same earliest time, next ID (20)");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.id == 30, "Tie-break 3rd: same earliest time, highest ID (30)");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.id == 10, "Tie-break 4th: later timestamp (t1)");

    priority_queue_destroy(&pq);
}

static void test_dynamic_resizing(void) {
    PriorityQueue pq;
    priority_queue_init(&pq, 4);

    for (int i = 1; i <= 30; i++) {
        Transaction tx;
        char time_buf[32];
        snprintf(time_buf, sizeof(time_buf), "2026-11-15 10:%02d:00", i);
        make_test_tx(&tx, i, i * 1000, time_buf);
        priority_queue_insert(&pq, &tx);
    }

    EXPECT_TRUE(priority_queue_get_size(&pq) == 30, "Size is 30 after growing beyond initial capacity 4");
    EXPECT_TRUE(pq.capacity >= 30, "Capacity dynamically expanded");

    int prev_amount = 99999999;
    int success = 1;
    for (int i = 0; i < 30; i++) {
        Transaction out;
        priority_queue_extract_max(&pq, &out);
        if (out.amount_paise > prev_amount) {
            success = 0;
            break;
        }
        prev_amount = out.amount_paise;
    }
    EXPECT_TRUE(success == 1, "Extracted all 30 elements in strict descending order");

    priority_queue_destroy(&pq);
}

static void test_duplicate_rejection(void) {
    PriorityQueue pq;
    priority_queue_init(&pq, 8);

    Transaction t1, t1_dup;
    make_test_tx(&t1, 77, 50000, "2026-11-15 10:00:00");
    make_test_tx(&t1_dup, 77, 99999, "2026-11-15 10:00:00");

    EXPECT_TRUE(priority_queue_insert(&pq, &t1) == 0, "Insert initial item");
    EXPECT_TRUE(priority_queue_has_id(&pq, 77) == 1, "PQ has ID 77");
    EXPECT_TRUE(priority_queue_insert(&pq, &t1_dup) == -3, "Insert duplicate ID rejected with -3");
    EXPECT_TRUE(priority_queue_get_size(&pq) == 1, "Size remains 1");

    priority_queue_destroy(&pq);
}

static void test_remove_by_id(void) {
    PriorityQueue pq;
    priority_queue_init(&pq, 16);

    Transaction t1, t2, t3, t4;
    make_test_tx(&t1, 1, 1000, "2026-11-15 10:00:00");
    make_test_tx(&t2, 2, 4000, "2026-11-15 10:00:00");
    make_test_tx(&t3, 3, 2000, "2026-11-15 10:00:00");
    make_test_tx(&t4, 4, 3000, "2026-11-15 10:00:00");

    priority_queue_insert(&pq, &t1);
    priority_queue_insert(&pq, &t2);
    priority_queue_insert(&pq, &t3);
    priority_queue_insert(&pq, &t4);

    EXPECT_TRUE(priority_queue_remove_by_id(&pq, 999) == -1, "Remove nonexistent ID returns -1");
    EXPECT_TRUE(priority_queue_remove_by_id(&pq, 2) == 0, "Remove root item (ID 2, 4000)");
    EXPECT_TRUE(priority_queue_get_size(&pq) == 3, "Size is 3");

    Transaction out;
    EXPECT_TRUE(priority_queue_peek(&pq, &out) == 0 && out.id == 4, "New root is ID 4 (3000)");

    EXPECT_TRUE(priority_queue_remove_by_id(&pq, 1) == 0, "Remove leaf item (ID 1, 1000)");
    EXPECT_TRUE(priority_queue_get_size(&pq) == 2, "Size is 2");

    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.id == 4, "Extract remaining max is ID 4");
    EXPECT_TRUE(priority_queue_extract_max(&pq, &out) == 0 && out.id == 3, "Extract final remaining is ID 3");

    priority_queue_destroy(&pq);
}

static void test_clear(void) {
    PriorityQueue pq;
    priority_queue_init(&pq, 8);

    Transaction t1;
    make_test_tx(&t1, 1, 100, "2026-11-15 10:00:00");
    priority_queue_insert(&pq, &t1);

    priority_queue_clear(&pq);
    EXPECT_TRUE(priority_queue_is_empty(&pq) == 1, "PQ is empty after clear");
    EXPECT_TRUE(priority_queue_get_size(&pq) == 0, "Size is 0 after clear");

    EXPECT_TRUE(priority_queue_insert(&pq, &t1) == 0, "Insert works after clear");
    EXPECT_TRUE(priority_queue_get_size(&pq) == 1, "Size is 1");

    priority_queue_destroy(&pq);
}

int main(void) {
    printf("Starting Max-Heap Priority Queue DSA Tests...\n");

    test_init_and_destroy();
    test_max_heap_order();
    test_deterministic_tie_breaking();
    test_dynamic_resizing();
    test_duplicate_rejection();
    test_remove_by_id();
    test_clear();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
