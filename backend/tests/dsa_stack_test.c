#include "../src/dsa/stack.h"
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
    tx->status = TRANSACTION_STATUS_COMPLETED;
    tx->transaction_type = TRANSACTION_TYPE_STANDARD;
    strncpy(tx->created_at, "2026-11-15 10:00:00", sizeof(tx->created_at) - 1);
}

static void test_init_and_destroy(void) {
    Stack s;
    EXPECT_TRUE(stack_init(&s, 50) == 0, "Stack init returns 0");
    EXPECT_TRUE(stack_is_empty(&s) == 1, "Stack is empty initially");
    EXPECT_TRUE(stack_get_size(&s) == 0, "Stack size is 0 initially");
    EXPECT_TRUE(s.capacity == 50, "Stack capacity is 50");
    stack_destroy(&s);
    EXPECT_TRUE(s.top == NULL && s.size == 0, "Stack top NULL and size 0 after destroy");
}

static void test_lifo_order(void) {
    Stack s;
    stack_init(&s, 10);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 10, 1000);
    make_test_tx(&t2, 20, 2000);
    make_test_tx(&t3, 30, 3000);

    EXPECT_TRUE(stack_push(&s, &t1) == 0, "Push t1");
    EXPECT_TRUE(stack_push(&s, &t2) == 0, "Push t2");
    EXPECT_TRUE(stack_push(&s, &t3) == 0, "Push t3");
    EXPECT_TRUE(stack_get_size(&s) == 3, "Stack size is 3");
    EXPECT_TRUE(stack_is_empty(&s) == 0, "Stack is not empty");

    Transaction out;
    EXPECT_TRUE(stack_peek(&s, &out) == 0 && out.id == 30, "Peek returns top item (t3)");
    EXPECT_TRUE(stack_get_size(&s) == 3, "Size unchanged after peek");

    EXPECT_TRUE(stack_pop(&s, &out) == 0 && out.id == 30, "Pop returns t3 (LIFO)");
    EXPECT_TRUE(stack_get_size(&s) == 2, "Size is 2 after pop");

    EXPECT_TRUE(stack_pop(&s, &out) == 0 && out.id == 20, "Pop returns t2");
    EXPECT_TRUE(stack_pop(&s, &out) == 0 && out.id == 10, "Pop returns t1");
    EXPECT_TRUE(stack_get_size(&s) == 0, "Size is 0 after popping all");
    EXPECT_TRUE(stack_is_empty(&s) == 1, "Stack is empty");
    EXPECT_TRUE(stack_pop(&s, &out) == -1, "Pop on empty returns -1");

    stack_destroy(&s);
}

static void test_bounded_capacity(void) {
    Stack s;
    stack_init(&s, 3);

    Transaction t1, t2, t3, t4;
    make_test_tx(&t1, 1, 100);
    make_test_tx(&t2, 2, 200);
    make_test_tx(&t3, 3, 300);
    make_test_tx(&t4, 4, 400);

    stack_push(&s, &t1);
    stack_push(&s, &t2);
    stack_push(&s, &t3);
    EXPECT_TRUE(stack_get_size(&s) == 3, "Stack at capacity 3");

    EXPECT_TRUE(stack_push(&s, &t4) == 0, "Push t4 beyond capacity");
    EXPECT_TRUE(stack_get_size(&s) == 3, "Size remains 3 after bounded eviction");

    Transaction out;
    EXPECT_TRUE(stack_pop(&s, &out) == 0 && out.id == 4, "Top is t4");
    EXPECT_TRUE(stack_pop(&s, &out) == 0 && out.id == 3, "Second is t3");
    EXPECT_TRUE(stack_pop(&s, &out) == 0 && out.id == 2, "Third is t2 (oldest t1 was evicted)");
    EXPECT_TRUE(stack_is_empty(&s) == 1, "Stack is now empty");

    stack_destroy(&s);
}

static void test_clear(void) {
    Stack s;
    stack_init(&s, 5);

    Transaction t1;
    make_test_tx(&t1, 1, 100);
    stack_push(&s, &t1);

    stack_clear(&s);
    EXPECT_TRUE(stack_is_empty(&s) == 1, "Stack is empty after clear");
    EXPECT_TRUE(stack_get_size(&s) == 0, "Size is 0 after clear");

    EXPECT_TRUE(stack_push(&s, &t1) == 0, "Push works after clear");
    EXPECT_TRUE(stack_get_size(&s) == 1, "Size is 1");

    stack_destroy(&s);
}

int main(void) {
    printf("Starting LIFO Stack DSA Tests...\n");

    test_init_and_destroy();
    test_lifo_order();
    test_bounded_capacity();
    test_clear();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
