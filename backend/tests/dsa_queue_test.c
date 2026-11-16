#include "../src/dsa/queue.h"
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

static void make_test_tx(Transaction *tx, long long id, int64_t amount_paise) {
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
    strncpy(tx->created_at, "2026-11-15 10:00:00", sizeof(tx->created_at) - 1);
}

static void test_init_and_destroy(void) {
    Queue q;
    EXPECT_TRUE(queue_init(&q, 100) == 0, "Queue init returns 0");
    EXPECT_TRUE(queue_is_empty(&q) == 1, "Queue is empty initially");
    EXPECT_TRUE(queue_get_size(&q) == 0, "Queue size is 0 initially");
    queue_destroy(&q);
    EXPECT_TRUE(q.front == NULL && q.rear == NULL, "Queue front and rear NULL after destroy");
}

static void test_fifo_order(void) {
    Queue q;
    queue_init(&q, 100);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 101, 1000);
    make_test_tx(&t2, 102, 2000);
    make_test_tx(&t3, 103, 3000);

    EXPECT_TRUE(queue_enqueue(&q, &t1) == 0, "Enqueue t1");
    EXPECT_TRUE(queue_enqueue(&q, &t2) == 0, "Enqueue t2");
    EXPECT_TRUE(queue_enqueue(&q, &t3) == 0, "Enqueue t3");
    EXPECT_TRUE(queue_get_size(&q) == 3, "Queue size is 3");
    EXPECT_TRUE(queue_is_empty(&q) == 0, "Queue is not empty");

    Transaction out;
    EXPECT_TRUE(queue_peek(&q, &out) == 0 && out.id == 101, "Peek returns first item (t1)");
    EXPECT_TRUE(queue_get_size(&q) == 3, "Size unchanged after peek");

    EXPECT_TRUE(queue_dequeue(&q, &out) == 0 && out.id == 101, "Dequeue returns t1");
    EXPECT_TRUE(queue_get_size(&q) == 2, "Size is 2 after first dequeue");

    EXPECT_TRUE(queue_dequeue(&q, &out) == 0 && out.id == 102, "Dequeue returns t2");
    EXPECT_TRUE(queue_dequeue(&q, &out) == 0 && out.id == 103, "Dequeue returns t3");
    EXPECT_TRUE(queue_get_size(&q) == 0, "Size is 0 after all dequeued");
    EXPECT_TRUE(queue_is_empty(&q) == 1, "Queue is empty");
    EXPECT_TRUE(queue_dequeue(&q, &out) == -1, "Dequeue on empty returns -1");

    queue_destroy(&q);
}

static void test_duplicate_rejection(void) {
    Queue q;
    queue_init(&q, 100);

    Transaction t1, t1_dup;
    make_test_tx(&t1, 100, 5000);
    make_test_tx(&t1_dup, 100, 9999);

    EXPECT_TRUE(queue_enqueue(&q, &t1) == 0, "Enqueue initial transaction");
    EXPECT_TRUE(queue_has_id(&q, 100) == 1, "Queue has ID 100");
    EXPECT_TRUE(queue_enqueue(&q, &t1_dup) == -3, "Enqueue duplicate ID rejected with -3");
    EXPECT_TRUE(queue_get_size(&q) == 1, "Size remains 1");

    queue_destroy(&q);
}

static void test_remove_by_id(void) {
    Queue q;
    queue_init(&q, 100);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 1, 100);
    make_test_tx(&t2, 2, 200);
    make_test_tx(&t3, 3, 300);

    queue_enqueue(&q, &t1);
    queue_enqueue(&q, &t2);
    queue_enqueue(&q, &t3);

    EXPECT_TRUE(queue_remove_by_id(&q, 999) == -1, "Remove nonexistent ID returns -1");
    EXPECT_TRUE(queue_remove_by_id(&q, 2) == 0, "Remove middle item (ID 2)");
    EXPECT_TRUE(queue_get_size(&q) == 2, "Size is 2");

    Transaction out;
    EXPECT_TRUE(queue_dequeue(&q, &out) == 0 && out.id == 1, "First remaining is ID 1");
    EXPECT_TRUE(queue_dequeue(&q, &out) == 0 && out.id == 3, "Second remaining is ID 3");

    queue_destroy(&q);
}

static void test_clear(void) {
    Queue q;
    queue_init(&q, 100);

    Transaction t1;
    make_test_tx(&t1, 1, 100);
    queue_enqueue(&q, &t1);

    queue_clear(&q);
    EXPECT_TRUE(queue_is_empty(&q) == 1, "Queue is empty after clear");
    EXPECT_TRUE(queue_get_size(&q) == 0, "Size is 0 after clear");

    EXPECT_TRUE(queue_enqueue(&q, &t1) == 0, "Enqueue works after clear");
    EXPECT_TRUE(queue_get_size(&q) == 1, "Size is 1");

    queue_destroy(&q);
}

int main(void) {
    printf("Starting FIFO Queue DSA Tests...\n");

    test_init_and_destroy();
    test_fifo_order();
    test_duplicate_rejection();
    test_remove_by_id();
    test_clear();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
