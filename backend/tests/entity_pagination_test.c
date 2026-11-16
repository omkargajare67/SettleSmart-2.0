#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/entities/entity_service.h"
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
    printf("Starting Entity Pagination, Search and Filter Tests...\n");

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

    long long org_id = create_test_org(&client, "Pagination Test Org");
    long long user_id = create_test_user(&client, org_id, "admin_pagi@test.com");
    EXPECT_TRUE(org_id > 0 && user_id > 0, "Test organization and user setup");

    char clean[256];
    snprintf(clean, sizeof(clean), "DELETE FROM entities WHERE organization_id = %lld;", org_id);
    db_client_query(&client, clean);

    struct {
        const char *name;
        EntityType type;
        EntityStatus status;
    } test_data[15] = {
        {"Dept Alpha", ENTITY_TYPE_DEPARTMENT, ENTITY_STATUS_ACTIVE},
        {"Dept Beta", ENTITY_TYPE_DEPARTMENT, ENTITY_STATUS_ACTIVE},
        {"Dept Gamma", ENTITY_TYPE_DEPARTMENT, ENTITY_STATUS_ACTIVE},
        {"Dept Delta", ENTITY_TYPE_DEPARTMENT, ENTITY_STATUS_INACTIVE},
        {"Dept Epsilon", ENTITY_TYPE_DEPARTMENT, ENTITY_STATUS_INACTIVE},
        {"Vendor Acme", ENTITY_TYPE_VENDOR, ENTITY_STATUS_ACTIVE},
        {"Vendor Global", ENTITY_TYPE_VENDOR, ENTITY_STATUS_ACTIVE},
        {"Vendor Prime", ENTITY_TYPE_VENDOR, ENTITY_STATUS_ACTIVE},
        {"Vendor Zenith", ENTITY_TYPE_VENDOR, ENTITY_STATUS_ACTIVE},
        {"Client Nexus", ENTITY_TYPE_CLIENT, ENTITY_STATUS_ACTIVE},
        {"Client Orion", ENTITY_TYPE_CLIENT, ENTITY_STATUS_ACTIVE},
        {"Client Titan", ENTITY_TYPE_CLIENT, ENTITY_STATUS_INACTIVE},
        {"Sub Solar", ENTITY_TYPE_SUBSIDIARY, ENTITY_STATUS_ACTIVE},
        {"Sub Lunar", ENTITY_TYPE_SUBSIDIARY, ENTITY_STATUS_ACTIVE},
        {"Partner Astra", ENTITY_TYPE_PARTNER, ENTITY_STATUS_ACTIVE}
    };

    long long first_created_id = 0;
    long long target_vendor_id = 0;

    char err_msg[256] = {0};
    for (int i = 0; i < 15; i++) {
        Entity created;
        int res = entity_service_create(&client, org_id, user_id, test_data[i].name,
                                       test_data[i].type, &created, err_msg, sizeof(err_msg));
        if (res != ENTITY_ERR_OK) {
            printf("FAIL: Seed entity %s failed: %s\n", test_data[i].name, err_msg);
        }
        if (i == 0) {
            first_created_id = created.id;
        }
        if (strcmp(test_data[i].name, "Vendor Zenith") == 0) {
            target_vendor_id = created.id;
        }
        if (test_data[i].status == ENTITY_STATUS_INACTIVE) {
            entity_service_update_status(&client, org_id, user_id, created.id,
                                        ENTITY_STATUS_INACTIVE, &created, err_msg, sizeof(err_msg));
        }
    }

    Entity list_buf[20];
    EntityPagination pagi;

    EntityFilter f_p1 = {.page = 1, .limit = 5};
    int count = entity_service_list(&client, org_id, &f_p1, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 5, "Page 1 returns 5 records");
    EXPECT_TRUE(pagi.page == 1 && pagi.limit == 5, "Pagination page and limit match");
    EXPECT_TRUE(pagi.total_records == 15, "Total records is 15");
    EXPECT_TRUE(pagi.total_pages == 3, "Total pages is 3");
    long long p1_first_id = list_buf[0].id;

    EntityFilter f_p2 = {.page = 2, .limit = 5};
    count = entity_service_list(&client, org_id, &f_p2, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 5, "Page 2 returns 5 records");
    EXPECT_TRUE(list_buf[0].id != p1_first_id, "Page 2 records distinct from Page 1");

    EntityFilter f_p3 = {.page = 3, .limit = 5};
    count = entity_service_list(&client, org_id, &f_p3, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 5, "Page 3 returns 5 records");

    EntityFilter f_p4 = {.page = 4, .limit = 5};
    count = entity_service_list(&client, org_id, &f_p4, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 0, "Page 4 returns 0 records");

    EntityFilter f_search = {.page = 1, .limit = 10, .search = "Zenith"};
    count = entity_service_list(&client, org_id, &f_search, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 1, "Search 'Zenith' returns 1 record");
    EXPECT_TRUE(strcmp(list_buf[0].name, "Vendor Zenith") == 0, "Search result matches expected name");

    EntityFilter f_search_dept = {.page = 1, .limit = 10, .search = "Dept"};
    count = entity_service_list(&client, org_id, &f_search_dept, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 5, "Search 'Dept' returns 5 records");

    char id_search_str[32];
    snprintf(id_search_str, sizeof(id_search_str), "%lld", target_vendor_id);
    EntityFilter f_search_id = {.page = 1, .limit = 10};
    strncpy(f_search_id.search, id_search_str, sizeof(f_search_id.search) - 1);
    count = entity_service_list(&client, org_id, &f_search_id, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 1, "Search by numeric ID returns 1 record");
    EXPECT_TRUE(list_buf[0].id == target_vendor_id, "Numeric ID search matches exact entity");

    EntityFilter f_type = {.page = 1, .limit = 10, .type_filter = ENTITY_TYPE_VENDOR};
    count = entity_service_list(&client, org_id, &f_type, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 4, "Filter by Vendor returns 4 records");
    for (int i = 0; i < count; i++) {
        EXPECT_TRUE(list_buf[i].entity_type == ENTITY_TYPE_VENDOR, "Every filtered item is Vendor");
    }

    EntityFilter f_status = {.page = 1, .limit = 10, .status_filter = ENTITY_STATUS_INACTIVE};
    count = entity_service_list(&client, org_id, &f_status, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 3, "Filter by Inactive status returns 3 records");
    for (int i = 0; i < count; i++) {
        EXPECT_TRUE(list_buf[i].status == ENTITY_STATUS_INACTIVE, "Every filtered item is Inactive");
    }

    EntityFilter f_sort_asc = {.page = 1, .limit = 10, .sort_by = "name", .order = "ASC"};
    count = entity_service_list(&client, org_id, &f_sort_asc, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 10, "Sort ASC returns 10 records");
    EXPECT_TRUE(strcmp(list_buf[0].name, "Client Nexus") == 0, "First record alphabetically is Client Nexus");

    EntityFilter f_sort_desc = {.page = 1, .limit = 10, .sort_by = "name", .order = "DESC"};
    count = entity_service_list(&client, org_id, &f_sort_desc, list_buf, 20, &pagi, err_msg, sizeof(err_msg));
    EXPECT_TRUE(count == 10, "Sort DESC returns 10 records");
    EXPECT_TRUE(strcmp(list_buf[0].name, "Vendor Zenith") == 0, "First record reverse alphabetically is Vendor Zenith");

    db_client_close(&client);

    printf("\nEntity Pagination Tests Completed. Passed: %d, Failed: %d\n",
           g_tests_passed, g_tests_failed);

    return g_tests_failed == 0 ? 0 : 1;
}
