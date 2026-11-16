#include "../src/dsa/linked_list.h"
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
    LinkedList list;
    EXPECT_TRUE(linked_list_init(&list, 10) == 0, "LinkedList init with capacity 10");
    EXPECT_TRUE(linked_list_get_size(&list) == 0, "LinkedList initial size is 0");
    EXPECT_TRUE(list.head == NULL && list.tail == NULL, "LinkedList head and tail are NULL");
    linked_list_destroy(&list);
    EXPECT_TRUE(list.size == 0 && list.capacity == 0, "LinkedList destroyed state is empty");
}

static void test_insert(void) {
    LinkedList list;
    linked_list_init(&list, 5);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 1, 1000);
    make_test_tx(&t2, 2, 2000);
    make_test_tx(&t3, 3, 3000);

    EXPECT_TRUE(linked_list_insert(&list, &t1) == 0, "Insert t1");
    EXPECT_TRUE(linked_list_insert(&list, &t2) == 0, "Insert t2");
    EXPECT_TRUE(linked_list_insert(&list, &t3) == 0, "Insert t3");

    EXPECT_TRUE(linked_list_get_size(&list) == 3, "Size after 3 inserts is 3");
    EXPECT_TRUE(list.head->tx.id == 3, "Head is t3 (most recent)");
    EXPECT_TRUE(list.head->next->tx.id == 2, "Second is t2");
    EXPECT_TRUE(list.tail->tx.id == 1, "Tail is t1");

    linked_list_destroy(&list);
}

static void test_capacity_eviction(void) {
    LinkedList list;
    linked_list_init(&list, 3);

    Transaction t1, t2, t3, t4;
    make_test_tx(&t1, 1, 1000);
    make_test_tx(&t2, 2, 2000);
    make_test_tx(&t3, 3, 3000);
    make_test_tx(&t4, 4, 4000);

    linked_list_insert(&list, &t1);
    linked_list_insert(&list, &t2);
    linked_list_insert(&list, &t3);
    EXPECT_TRUE(linked_list_get_size(&list) == 3, "Size at capacity is 3");

    EXPECT_TRUE(linked_list_insert(&list, &t4) == 0, "Insert t4 beyond capacity");
    EXPECT_TRUE(linked_list_get_size(&list) == 3, "Size remains 3 after eviction");
    EXPECT_TRUE(list.head->tx.id == 4, "Head is now t4");
    EXPECT_TRUE(list.tail->tx.id == 2, "Tail is now t2 (oldest t1 was evicted)");

    linked_list_destroy(&list);
}

static int g_visit_count = 0;
static void count_visitor(const Transaction *tx, void *user_data) {
    (void)tx;
    int *c = (int *)user_data;
    (*c)++;
}

static void test_traversal(void) {
    LinkedList list;
    linked_list_init(&list, 10);

    for (int i = 1; i <= 5; i++) {
        Transaction tx;
        make_test_tx(&tx, i, i * 1000);
        linked_list_insert(&list, &tx);
    }

    g_visit_count = 0;
    linked_list_traverse(&list, count_visitor, &g_visit_count);
    EXPECT_TRUE(g_visit_count == 5, "Traverse visited exactly 5 elements");

    linked_list_destroy(&list);
}

static void test_remove_by_id(void) {
    LinkedList list;
    linked_list_init(&list, 10);

    Transaction t1, t2, t3;
    make_test_tx(&t1, 10, 1000);
    make_test_tx(&t2, 20, 2000);
    make_test_tx(&t3, 30, 3000);

    linked_list_insert(&list, &t1);
    linked_list_insert(&list, &t2);
    linked_list_insert(&list, &t3);

    EXPECT_TRUE(linked_list_remove_by_id(&list, 999) == -1, "Remove nonexistent ID returns -1");
    EXPECT_TRUE(linked_list_remove_by_id(&list, 20) == 0, "Remove middle node (id 20)");
    EXPECT_TRUE(linked_list_get_size(&list) == 2, "Size is 2 after middle removal");
    EXPECT_TRUE(list.head->tx.id == 30 && list.tail->tx.id == 10, "Head is 30, Tail is 10");

    EXPECT_TRUE(linked_list_remove_by_id(&list, 30) == 0, "Remove head node (id 30)");
    EXPECT_TRUE(linked_list_get_size(&list) == 1, "Size is 1 after head removal");
    EXPECT_TRUE(list.head->tx.id == 10 && list.tail->tx.id == 10, "Head and Tail are 10");

    EXPECT_TRUE(linked_list_remove_by_id(&list, 10) == 0, "Remove only remaining node (id 10)");
    EXPECT_TRUE(linked_list_get_size(&list) == 0, "Size is 0 after removing last node");
    EXPECT_TRUE(list.head == NULL && list.tail == NULL, "Head and Tail are NULL");

    linked_list_destroy(&list);
}

static void test_clear(void) {
    LinkedList list;
    linked_list_init(&list, 5);

    Transaction t1;
    make_test_tx(&t1, 1, 1000);
    linked_list_insert(&list, &t1);

    linked_list_clear(&list);
    EXPECT_TRUE(linked_list_get_size(&list) == 0, "Size is 0 after clear");
    EXPECT_TRUE(list.head == NULL && list.tail == NULL, "Head and tail NULL after clear");

    EXPECT_TRUE(linked_list_insert(&list, &t1) == 0, "Insert works after clear");
    EXPECT_TRUE(linked_list_get_size(&list) == 1, "Size is 1");

    linked_list_destroy(&list);
}

int main(void) {
    printf("Starting Singly Linked List DSA Tests...\n");

    test_init_and_destroy();
    test_insert();
    test_capacity_eviction();
    test_traversal();
    test_remove_by_id();
    test_clear();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
