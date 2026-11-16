#include "../src/algorithms/binary_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define EXPECT_TRUE(cond, desc) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", desc); \
    } else { \
        printf("  [FAIL] %s (line %d)\n", desc, __LINE__); \
    } \
} while(0)

int main(void) {
    printf("=== RUNNING BINARY SEARCH ALGORITHM TESTS ===\n");

    {
        Transaction arr[5];
        memset(arr, 0, sizeof(arr));
        arr[0].id = 10;
        arr[1].id = 20;
        arr[2].id = 30;
        arr[3].id = 40;
        arr[4].id = 50;

        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 10) == 0, "Find first element in transactions");
        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 50) == 4, "Find last element in transactions");
        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 30) == 2, "Find middle element in transactions");
        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 20) == 1, "Find second element in transactions");
        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 40) == 3, "Find fourth element in transactions");

        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 5) == -1, "Target smaller than minimum returns -1");
        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 55) == -1, "Target greater than maximum returns -1");
        EXPECT_TRUE(binary_search_transactions_by_id(arr, 5, 25) == -1, "Target in between elements returns -1");
        EXPECT_TRUE(binary_search_transactions_by_id(NULL, 0, 10) == -1, "Empty transactions array returns -1");
    }

    {
        Transaction single;
        memset(&single, 0, sizeof(single));
        single.id = 100;

        EXPECT_TRUE(binary_search_transactions_by_id(&single, 1, 100) == 0, "Single element match returns 0");
        EXPECT_TRUE(binary_search_transactions_by_id(&single, 1, 99) == -1, "Single element mismatch returns -1");
    }

    {
        EntityBalance arr[4];
        arr[0].entity_id = 1; arr[0].balance_paise = -5000;
        arr[1].entity_id = 3; arr[1].balance_paise = 2000;
        arr[2].entity_id = 7; arr[2].balance_paise = 3000;
        arr[3].entity_id = 9; arr[3].balance_paise = 0;

        EXPECT_TRUE(binary_search_balances_by_entity_id(arr, 4, 1) == 0, "Find first entity balance");
        EXPECT_TRUE(binary_search_balances_by_entity_id(arr, 4, 9) == 3, "Find last entity balance");
        EXPECT_TRUE(binary_search_balances_by_entity_id(arr, 4, 3) == 1, "Find intermediate entity balance");
        EXPECT_TRUE(binary_search_balances_by_entity_id(arr, 4, 7) == 2, "Find third entity balance");

        EXPECT_TRUE(binary_search_balances_by_entity_id(arr, 4, 0) == -1, "Missing entity balance returns -1");
        EXPECT_TRUE(binary_search_balances_by_entity_id(arr, 4, 5) == -1, "In-between missing entity balance returns -1");
        EXPECT_TRUE(binary_search_balances_by_entity_id(arr, 4, 10) == -1, "Above range entity balance returns -1");
        EXPECT_TRUE(binary_search_balances_by_entity_id(NULL, 0, 1) == -1, "Null array returns -1");
    }

    printf("\nBinary Search Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
