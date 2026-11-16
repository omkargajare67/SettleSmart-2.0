#include "../src/dsa/dsa_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static void make_tx(Transaction *tx, long long id, long long org_id, long long sender, long long receiver,
                    int64_t paise, TransactionStatus status, TransactionType type, const char *created_at) {
    memset(tx, 0, sizeof(Transaction));
    tx->id = id;
    tx->organization_id = org_id;
    tx->sender_entity_id = sender;
    tx->receiver_entity_id = receiver;
    tx->amount_paise = paise;
    snprintf(tx->amount_str, sizeof(tx->amount_str), "%.2f", (double)paise / 100.0);
    snprintf(tx->currency, sizeof(tx->currency), "INR");
    tx->status = status;
    tx->transaction_type = type;
    strncpy(tx->created_at, created_at, sizeof(tx->created_at) - 1);
}

int main(void) {
    printf("Starting DSA Transaction Synchronization and 10,000 Volume Performance Tests...\n");

    long long org_id = 42;
    DsaState state;
    EXPECT_TRUE(dsa_state_init(&state, org_id) == 0, "Initialize DsaState for sync test");

    long long e1 = 1001;
    long long e2 = 1002;
    hashmap_insert(&state.entity_balances, e1, 0);
    hashmap_insert(&state.entity_balances, e2, 0);

    Transaction tx1;
    make_tx(&tx1, 1, org_id, e1, e2, 500000, TRANSACTION_STATUS_PENDING, TRANSACTION_TYPE_STANDARD, "2026-11-15 10:00:00");
    EXPECT_TRUE(dsa_state_on_transaction_created(&state, &tx1) == 0, "On transaction created: tx1 Pending");

    int64_t b1 = 0, b2 = 0;
    hashmap_get_balance(&state.entity_balances, e1, &b1);
    hashmap_get_balance(&state.entity_balances, e2, &b2);
    EXPECT_TRUE(b1 == 0 && b2 == 0, "Pending transaction does not modify balances");
    EXPECT_TRUE(queue_get_size(&state.pending_queue) == 1, "Pending transaction added to FIFO queue");
    EXPECT_TRUE(priority_queue_get_size(&state.pending_priority_queue) == 1, "Pending transaction added to Priority Queue");
    EXPECT_TRUE(stack_get_size(&state.completed_stack) == 0, "Pending transaction not on Completed stack");

    tx1.status = TRANSACTION_STATUS_COMPLETED;
    EXPECT_TRUE(dsa_state_on_transaction_status_changed(&state, &tx1, TRANSACTION_STATUS_PENDING) == 0,
                "On transaction status changed: tx1 Pending -> Completed");

    hashmap_get_balance(&state.entity_balances, e1, &b1);
    hashmap_get_balance(&state.entity_balances, e2, &b2);
    EXPECT_TRUE(b1 == -500000 && b2 == 500000, "Completed transaction updates balances: e1=-5000.00, e2=+5000.00");
    EXPECT_TRUE(queue_get_size(&state.pending_queue) == 0, "Completed transaction removed from FIFO queue");
    EXPECT_TRUE(priority_queue_get_size(&state.pending_priority_queue) == 0, "Completed transaction removed from Priority Queue");
    EXPECT_TRUE(stack_get_size(&state.completed_stack) == 1, "Completed transaction pushed to Completed stack");

    Transaction tx_rev;
    make_tx(&tx_rev, 2, org_id, e2, e1, 500000, TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_REVERSAL, "2026-11-15 10:05:00");
    tx_rev.reversal_of_id = 1;
    EXPECT_TRUE(dsa_state_on_transaction_created(&state, &tx_rev) == 0, "On transaction created: reversal tx_rev Completed");

    hashmap_get_balance(&state.entity_balances, e1, &b1);
    hashmap_get_balance(&state.entity_balances, e2, &b2);
    EXPECT_TRUE(b1 == 0 && b2 == 0, "Reversal transaction restored both balances to 0");
    EXPECT_TRUE(stack_get_size(&state.completed_stack) == 2, "Reversal transaction pushed to Completed stack");

    printf("\nStarting 10,000 transaction performance stress test...\n");
    clock_t start = clock();

    for (long long i = 100; i < 10100; i++) {
        Transaction tx;
        long long s_ent = 2000 + (i % 50);
        long long r_ent = 3000 + (i % 50);
        make_tx(&tx, i, org_id, s_ent, r_ent, (i % 1000 + 1) * 100,
                TRANSACTION_STATUS_COMPLETED, TRANSACTION_TYPE_STANDARD, "2026-11-15 11:00:00");
        dsa_state_on_transaction_created(&state, &tx);
    }

    clock_t end = clock();
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    printf("10,000 transactions processed in %.2f ms\n", elapsed_ms);

    EXPECT_TRUE(elapsed_ms < 2000.0, "10,000 transactions processed in under 2000 ms");
    EXPECT_TRUE(linked_list_get_size(&state.recent_transactions) == 500, "Recent transactions bounded at capacity 500");
    EXPECT_TRUE(stack_get_size(&state.completed_stack) == 100, "Completed stack bounded at capacity 100");

    dsa_state_destroy(&state);

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
