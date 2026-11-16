#include "../src/dsa/hashmap.h"
#include <stdio.h>
#include <stdint.h>

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

static void test_init_and_destroy(void) {
    HashMap map;
    EXPECT_TRUE(hashmap_init(&map, 16) == 0, "HashMap init with 16 buckets");
    EXPECT_TRUE(hashmap_get_size(&map) == 0, "Initial size is 0");
    EXPECT_TRUE(map.bucket_count == 16, "Bucket count is 16");
    hashmap_destroy(&map);
    EXPECT_TRUE(map.buckets == NULL && map.size == 0, "Destroyed map is empty");
}

static void test_insert_and_lookup(void) {
    HashMap map;
    hashmap_init(&map, 16);

    EXPECT_TRUE(hashmap_insert(&map, 101, 500000) == 0, "Insert entity 101 with balance 500000");
    EXPECT_TRUE(hashmap_insert(&map, 202, -250000) == 0, "Insert entity 202 with balance -250000");
    EXPECT_TRUE(hashmap_insert(&map, 303, 0) == 0, "Insert entity 303 with balance 0");
    EXPECT_TRUE(hashmap_get_size(&map) == 3, "Size is 3");

    int64_t bal = 0;
    EXPECT_TRUE(hashmap_get_balance(&map, 101, &bal) == 0 && bal == 500000, "Lookup 101 returns 500000");
    EXPECT_TRUE(hashmap_get_balance(&map, 202, &bal) == 0 && bal == -250000, "Lookup 202 returns -250000");
    EXPECT_TRUE(hashmap_get_balance(&map, 303, &bal) == 0 && bal == 0, "Lookup 303 returns 0");
    EXPECT_TRUE(hashmap_get_balance(&map, 999, &bal) == -1, "Lookup nonexistent 999 returns -1");

    EXPECT_TRUE(hashmap_insert(&map, 101, 750000) == 0, "Update existing 101 balance to 750000");
    EXPECT_TRUE(hashmap_get_balance(&map, 101, &bal) == 0 && bal == 750000, "Lookup 101 returns updated 750000");
    EXPECT_TRUE(hashmap_get_size(&map) == 3, "Size remains 3 after in-place update");

    hashmap_destroy(&map);
}

static void test_collision_handling(void) {
    HashMap map;
    hashmap_init(&map, 10);

    long long key1 = 5;
    long long key2 = 15;
    long long key3 = 25;

    EXPECT_TRUE(hashmap_insert(&map, key1, 100) == 0, "Insert colliding key 5");
    EXPECT_TRUE(hashmap_insert(&map, key2, 200) == 0, "Insert colliding key 15");
    EXPECT_TRUE(hashmap_insert(&map, key3, 300) == 0, "Insert colliding key 25");
    EXPECT_TRUE(hashmap_get_size(&map) == 3, "Size is 3 with 3 colliding keys");

    int64_t bal = 0;
    EXPECT_TRUE(hashmap_get_balance(&map, 5, &bal) == 0 && bal == 100, "Lookup 5 in collision chain");
    EXPECT_TRUE(hashmap_get_balance(&map, 15, &bal) == 0 && bal == 200, "Lookup 15 in collision chain");
    EXPECT_TRUE(hashmap_get_balance(&map, 25, &bal) == 0 && bal == 300, "Lookup 25 in collision chain");

    EXPECT_TRUE(hashmap_remove(&map, 15) == 0, "Remove middle node in collision chain");
    EXPECT_TRUE(hashmap_get_balance(&map, 15, &bal) == -1, "Lookup 15 returns -1 after remove");
    EXPECT_TRUE(hashmap_get_balance(&map, 5, &bal) == 0 && bal == 100, "Key 5 still present");
    EXPECT_TRUE(hashmap_get_balance(&map, 25, &bal) == 0 && bal == 300, "Key 25 still present");

    hashmap_destroy(&map);
}

static void test_balance_updates(void) {
    HashMap map;
    hashmap_init(&map, 16);

    EXPECT_TRUE(hashmap_update_balance(&map, 50, 100000) == 0, "Initial update for new key creates it with 100000");
    int64_t bal = 0;
    hashmap_get_balance(&map, 50, &bal);
    EXPECT_TRUE(bal == 100000, "Balance is 100000");

    EXPECT_TRUE(hashmap_update_balance(&map, 50, -40000) == 0, "Subtract 40000");
    hashmap_get_balance(&map, 50, &bal);
    EXPECT_TRUE(bal == 60000, "Balance is 60000");

    EXPECT_TRUE(hashmap_update_balance(&map, 50, -100000) == 0, "Subtract 100000 causing negative balance");
    hashmap_get_balance(&map, 50, &bal);
    EXPECT_TRUE(bal == -40000, "Balance is -40000");

    EXPECT_TRUE(hashmap_update_balance(&map, 50, 40000) == 0, "Add 40000 to reach zero");
    hashmap_get_balance(&map, 50, &bal);
    EXPECT_TRUE(bal == 0, "Balance is 0");

    hashmap_destroy(&map);
}

static void test_overflow_protection(void) {
    HashMap map;
    hashmap_init(&map, 8);

    hashmap_insert(&map, 1, INT64_MAX - 100);
    EXPECT_TRUE(hashmap_update_balance(&map, 1, 200) == -2, "Overflow detection returns error code");
    int64_t bal = 0;
    hashmap_get_balance(&map, 1, &bal);
    EXPECT_TRUE(bal == INT64_MAX - 100, "Balance unchanged after prevented overflow");

    hashmap_insert(&map, 2, INT64_MIN + 100);
    EXPECT_TRUE(hashmap_update_balance(&map, 2, -200) == -2, "Underflow detection returns error code");
    hashmap_get_balance(&map, 2, &bal);
    EXPECT_TRUE(bal == INT64_MIN + 100, "Balance unchanged after prevented underflow");

    hashmap_destroy(&map);
}

static void test_remove_and_clear(void) {
    HashMap map;
    hashmap_init(&map, 8);

    hashmap_insert(&map, 10, 100);
    hashmap_insert(&map, 20, 200);

    EXPECT_TRUE(hashmap_remove(&map, 999) == -1, "Remove nonexistent key returns -1");
    EXPECT_TRUE(hashmap_remove(&map, 10) == 0, "Remove key 10 returns 0");
    EXPECT_TRUE(hashmap_get_size(&map) == 1, "Size is 1");

    hashmap_clear(&map);
    EXPECT_TRUE(hashmap_get_size(&map) == 0, "Size is 0 after clear");
    int64_t bal = 0;
    EXPECT_TRUE(hashmap_get_balance(&map, 20, &bal) == -1, "Lookup 20 returns -1 after clear");

    hashmap_destroy(&map);
}

int main(void) {
    printf("Starting HashMap Separate Chaining Tests...\n");

    test_init_and_destroy();
    test_insert_and_lookup();
    test_collision_handling();
    test_balance_updates();
    test_overflow_protection();
    test_remove_and_clear();

    printf("\nTest Summary: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
