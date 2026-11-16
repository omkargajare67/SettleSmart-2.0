#include "../src/algorithms/bubble_sort.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
    printf("=== RUNNING BUBBLE SORT ALGORITHM TESTS ===\n");

    {
        Transaction arr[4];
        memset(arr, 0, sizeof(arr));

        arr[0].id = 1; arr[0].amount_paise = 3000; strcpy(arr[0].created_at, "2026-01-01 10:00:00");
        arr[1].id = 2; arr[1].amount_paise = 1000; strcpy(arr[1].created_at, "2026-01-01 10:01:00");
        arr[2].id = 3; arr[2].amount_paise = 4000; strcpy(arr[2].created_at, "2026-01-01 10:02:00");
        arr[3].id = 4; arr[3].amount_paise = 2000; strcpy(arr[3].created_at, "2026-01-01 10:03:00");

        bubble_sort_transactions(arr, 4, SORT_BY_AMOUNT, SORT_ORDER_ASC);
        EXPECT_TRUE(arr[0].amount_paise == 1000 && arr[1].amount_paise == 2000 &&
                    arr[2].amount_paise == 3000 && arr[3].amount_paise == 4000,
                    "Sort by amount ascending");

        bubble_sort_transactions(arr, 4, SORT_BY_AMOUNT, SORT_ORDER_DESC);
        EXPECT_TRUE(arr[0].amount_paise == 4000 && arr[1].amount_paise == 3000 &&
                    arr[2].amount_paise == 2000 && arr[3].amount_paise == 1000,
                    "Sort by amount descending");
    }

    {
        Transaction arr[3];
        memset(arr, 0, sizeof(arr));

        arr[0].id = 1; arr[0].amount_paise = 5000; strcpy(arr[0].created_at, "2026-01-01 12:00:00");
        arr[1].id = 2; arr[1].amount_paise = 5000; strcpy(arr[1].created_at, "2026-01-01 11:00:00");
        arr[2].id = 3; arr[2].amount_paise = 5000; strcpy(arr[2].created_at, "2026-01-01 13:00:00");

        bubble_sort_transactions(arr, 3, SORT_BY_AMOUNT, SORT_ORDER_ASC);
        EXPECT_TRUE(arr[0].id == 2 && arr[1].id == 1 && arr[2].id == 3,
                    "Deterministic tie-breaking on equal amounts breaks by timestamp");
    }

    {
        Transaction arr[2];
        memset(arr, 0, sizeof(arr));

        arr[0].id = 5; arr[0].amount_paise = 5000; strcpy(arr[0].created_at, "2026-01-01 12:00:00");
        arr[1].id = 3; arr[1].amount_paise = 5000; strcpy(arr[1].created_at, "2026-01-01 12:00:00");

        bubble_sort_transactions(arr, 2, SORT_BY_AMOUNT, SORT_ORDER_ASC);
        EXPECT_TRUE(arr[0].id == 3 && arr[1].id == 5,
                    "Deterministic tie-breaking on equal amounts and timestamps breaks by ID");
    }

    {
        Transaction arr[3];
        memset(arr, 0, sizeof(arr));

        arr[0].id = 10; arr[0].amount_paise = 100; strcpy(arr[0].created_at, "2026-01-01 15:00:00");
        arr[1].id = 20; arr[1].amount_paise = 200; strcpy(arr[1].created_at, "2026-01-01 09:00:00");
        arr[2].id = 30; arr[2].amount_paise = 300; strcpy(arr[2].created_at, "2026-01-01 12:00:00");

        bubble_sort_transactions(arr, 3, SORT_BY_TIMESTAMP, SORT_ORDER_ASC);
        EXPECT_TRUE(arr[0].id == 20 && arr[1].id == 30 && arr[2].id == 10,
                    "Sort by timestamp ascending");

        bubble_sort_transactions(arr, 3, SORT_BY_TIMESTAMP, SORT_ORDER_DESC);
        EXPECT_TRUE(arr[0].id == 10 && arr[1].id == 30 && arr[2].id == 20,
                    "Sort by timestamp descending");
    }

    {
        Transaction arr[3];
        memset(arr, 0, sizeof(arr));

        arr[0].id = 70;
        arr[1].id = 20;
        arr[2].id = 50;

        bubble_sort_transactions(arr, 3, SORT_BY_ID, SORT_ORDER_ASC);
        EXPECT_TRUE(arr[0].id == 20 && arr[1].id == 50 && arr[2].id == 70,
                    "Sort by ID ascending");

        bubble_sort_transactions(arr, 3, SORT_BY_ID, SORT_ORDER_DESC);
        EXPECT_TRUE(arr[0].id == 70 && arr[1].id == 50 && arr[2].id == 20,
                    "Sort by ID descending");
    }

    {
        Transaction single;
        memset(&single, 0, sizeof(single));
        single.id = 42;
        bubble_sort_transactions(&single, 1, SORT_BY_ID, SORT_ORDER_ASC);
        EXPECT_TRUE(single.id == 42, "Sort single element array");

        bubble_sort_transactions(NULL, 0, SORT_BY_ID, SORT_ORDER_ASC);
        EXPECT_TRUE(1, "Sort empty array safely");
    }

    printf("\nBubble Sort Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
