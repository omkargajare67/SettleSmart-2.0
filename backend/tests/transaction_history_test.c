#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/entities/entity_service.h"
#include "../src/transactions/transaction_service.h"
#include <stdio.h>
#include <stdlib.h>
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

static long long create_test_org(DBClient *client, const char *name) {
    char query[256];
    snprintf(query, sizeof(query),
             "INSERT INTO organizations (name, status) VALUES ('%s', 'Active') "
             "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id);", name);
    if (db_client_query(client, query) != 0) {
        return -1;
    }
    return (long long)db_client_last_insert_id(client);
}

static long long create_test_user(DBClient *client, long long org_id, const char *email) {
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO users (organization_id, email, password_hash, role, status) "
             "VALUES (%lld, '%s', 'hash', 'Admin', 'Active') "
             "ON DUPLICATE KEY UPDATE id=LAST_INSERT_ID(id);", org_id, email);
    if (db_client_query(client, query) != 0) {
        return -1;
    }
    return (long long)db_client_last_insert_id(client);
}

int main(void) {
    printf("Starting Transaction History, Search, Pagination and Recent Tests...\n");

    load_env_file(".env");
    load_env_file("../.env");

    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &db_cfg) != 0) {
        printf("FAIL: Database connection failed: %s\n", db_client_get_error(&client));
        return 1;
    }

    db_client_query(&client, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx History Org%');");
    db_client_query(&client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx History Org%');");
    db_client_query(&client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx History Org%');");
    db_client_query(&client, "DELETE FROM organizations WHERE name LIKE 'Tx History Org%';");

    long long org_id = create_test_org(&client, "Tx History Org");
    long long user_id = create_test_user(&client, org_id, "tx_history_user@org.com");
    EXPECT_TRUE(org_id > 0 && user_id > 0, "Create test org and user");

    Entity ent_a, ent_b, ent_c;
    char err_buf[256];
    entity_service_create(&client, org_id, user_id, "Hist Ent A", ENTITY_TYPE_SUBSIDIARY, &ent_a, err_buf, sizeof(err_buf));
    entity_service_create(&client, org_id, user_id, "Hist Ent B", ENTITY_TYPE_VENDOR, &ent_b, err_buf, sizeof(err_buf));
    entity_service_create(&client, org_id, user_id, "Hist Ent C", ENTITY_TYPE_CLIENT, &ent_c, err_buf, sizeof(err_buf));

    Transaction seed_txs[15];
    for (int i = 0; i < 15; i++) {
        char key[65];
        snprintf(key, sizeof(key), "f0000000-0000-4000-8000-%012d", i + 1);
        char amt[32];
        snprintf(amt, sizeof(amt), "%d00.00", i + 1);
        int is_rep = 0;
        long long s_id = (i % 2 == 0) ? ent_a.id : ent_b.id;
        long long r_id = (i % 2 == 0) ? ent_b.id : ent_c.id;

        int res = transaction_service_create(&client, org_id, user_id, s_id, r_id,
                                            amt, "INR", key,
                                            &seed_txs[i], &is_rep, err_buf, sizeof(err_buf));
        EXPECT_TRUE(res == TRANSACTION_ERR_OK, "Seed transaction created");
    }

    TransactionFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.page = 1;
    filter.limit = 5;

    Transaction results[20];
    TransactionPagination pagination;

    int count = transaction_service_list(&client, org_id, &filter, results, 20, &pagination, err_buf, sizeof(err_buf));
    EXPECT_TRUE(count == 5, "Page 1 returned 5 records");
    EXPECT_TRUE(pagination.total_records >= 15, "Total records is at least 15");
    EXPECT_TRUE(pagination.total_pages >= 3, "Total pages is at least 3");
    EXPECT_TRUE(pagination.page == 1, "Page index is 1");
    EXPECT_TRUE(pagination.limit == 5, "Limit is 5");
    long long first_page_first_id = results[0].id;

    filter.page = 2;
    count = transaction_service_list(&client, org_id, &filter, results, 20, &pagination, err_buf, sizeof(err_buf));
    EXPECT_TRUE(count == 5, "Page 2 returned 5 records");
    EXPECT_TRUE(results[0].id != first_page_first_id, "Page 2 does not overlap with Page 1");

    memset(&filter, 0, sizeof(filter));
    filter.transaction_id = seed_txs[3].id;
    count = transaction_service_list(&client, org_id, &filter, results, 20, &pagination, err_buf, sizeof(err_buf));
    EXPECT_TRUE(count == 1, "Filter by transaction_id returns exactly 1 record");
    EXPECT_TRUE(results[0].id == seed_txs[3].id, "Filtered ID matches target");

    memset(&filter, 0, sizeof(filter));
    filter.entity_id = ent_c.id;
    strcpy(filter.direction, "incoming");
    count = transaction_service_list(&client, org_id, &filter, results, 20, &pagination, err_buf, sizeof(err_buf));
    EXPECT_TRUE(count > 0, "Filter by entity incoming returned records");
    for (int i = 0; i < count; i++) {
        EXPECT_TRUE(results[i].receiver_entity_id == ent_c.id, "All returned records have ent_c as receiver");
    }

    memset(&filter, 0, sizeof(filter));
    strcpy(filter.min_amount, "400.00");
    strcpy(filter.max_amount, "700.00");
    count = transaction_service_list(&client, org_id, &filter, results, 20, &pagination, err_buf, sizeof(err_buf));
    EXPECT_TRUE(count > 0, "Filter by amount range returned records");
    for (int i = 0; i < count; i++) {
        EXPECT_TRUE(results[i].amount_paise >= 40000 && results[i].amount_paise <= 70000, "Amounts fall strictly within range");
    }

    memset(&filter, 0, sizeof(filter));
    strcpy(filter.sort_by, "amount");
    strcpy(filter.order, "ASC");
    filter.limit = 10;
    count = transaction_service_list(&client, org_id, &filter, results, 20, &pagination, err_buf, sizeof(err_buf));
    EXPECT_TRUE(count > 1, "Sort amount ASC returned records");
    for (int i = 1; i < count; i++) {
        EXPECT_TRUE(results[i].amount_paise >= results[i - 1].amount_paise, "Records sorted in ascending amount order");
    }

    memset(&filter, 0, sizeof(filter));
    strcpy(filter.sort_by, "amount");
    strcpy(filter.order, "DESC");
    filter.limit = 10;
    count = transaction_service_list(&client, org_id, &filter, results, 20, &pagination, err_buf, sizeof(err_buf));
    EXPECT_TRUE(count > 1, "Sort amount DESC returned records");
    for (int i = 1; i < count; i++) {
        EXPECT_TRUE(results[i].amount_paise <= results[i - 1].amount_paise, "Records sorted in descending amount order");
    }

    Transaction recent[10];
    int recent_count = 0;
    int res = transaction_service_get_recent(&client, org_id, 5, recent, 10, &recent_count, err_buf, sizeof(err_buf));
    EXPECT_TRUE(res == TRANSACTION_ERR_OK, "get_recent call succeeded");
    EXPECT_TRUE(recent_count == 5, "get_recent returned requested 5 items");
    for (int i = 1; i < recent_count; i++) {
        EXPECT_TRUE(recent[i].id < recent[i - 1].id, "Recent items sorted descending by id");
    }

    db_client_close(&client);

    printf("Transaction History Test Complete: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
