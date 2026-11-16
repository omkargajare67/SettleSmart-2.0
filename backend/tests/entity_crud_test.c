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
    printf("Starting Entity CRUD and Isolation Tests...\n");

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

    long long org1_id = create_test_org(&client, "Entity Test Org 1");
    long long org2_id = create_test_org(&client, "Entity Test Org 2");
    EXPECT_TRUE(org1_id > 0 && org2_id > 0 && org1_id != org2_id, "Test organizations established");

    long long user1_id = create_test_user(&client, org1_id, "admin1_crud@test.com");
    long long user2_id = create_test_user(&client, org2_id, "admin2_crud@test.com");
    EXPECT_TRUE(user1_id > 0 && user2_id > 0, "Test users established");

    db_client_query(&client, "DELETE FROM transactions;");
    char clean_entities[256];
    snprintf(clean_entities, sizeof(clean_entities),
             "DELETE FROM entities WHERE organization_id IN (%lld, %lld);",
             org1_id, org2_id);
    db_client_query(&client, clean_entities);

    char err_msg[256] = {0};
    Entity ent_a;
    int res = entity_service_create(&client, org1_id, user1_id, "Acme Logistics",
                                   ENTITY_TYPE_VENDOR, &ent_a, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Create Entity A succeeds");
    EXPECT_TRUE(ent_a.id > 0, "Entity A assigned positive auto-increment ID");
    EXPECT_TRUE(ent_a.organization_id == org1_id, "Entity A matches organization 1");
    EXPECT_TRUE(strcmp(ent_a.name, "Acme Logistics") == 0, "Entity A name matches");
    EXPECT_TRUE(ent_a.entity_type == ENTITY_TYPE_VENDOR, "Entity A type matches Vendor");
    EXPECT_TRUE(ent_a.status == ENTITY_STATUS_ACTIVE, "Entity A status defaults to Active");
    EXPECT_TRUE(ent_a.created_by_user_id == user1_id, "Entity A creator matches user 1");

    Entity ent_b;
    res = entity_service_create(&client, org1_id, user1_id, "Apex Holdings",
                               ENTITY_TYPE_CLIENT, &ent_b, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Create Entity B succeeds");
    EXPECT_TRUE(ent_b.id > ent_a.id, "Entity B ID strictly greater than Entity A (auto-increment)");

    Entity dup_ent;
    res = entity_service_create(&client, org1_id, user1_id, "Acme Logistics",
                               ENTITY_TYPE_SUBSIDIARY, &dup_ent, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_CONFLICT, "Duplicate entity name in same org rejected with CONFLICT");
    EXPECT_TRUE(strstr(err_msg, "already exists") != NULL, "Conflict error message returned");

    Entity org2_ent;
    res = entity_service_create(&client, org2_id, user2_id, "Acme Logistics",
                               ENTITY_TYPE_DEPARTMENT, &org2_ent, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Same entity name in different org permitted");
    EXPECT_TRUE(org2_ent.id > 0 && org2_ent.organization_id == org2_id,
                "Cross-org entity created with correct org ID");

    Entity fetched;
    res = entity_service_get_by_id(&client, org1_id, ent_a.id, &fetched, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Get Entity A by ID in Org 1 succeeds");
    EXPECT_TRUE(fetched.id == ent_a.id && strcmp(fetched.name, "Acme Logistics") == 0,
                "Fetched entity details match");

    res = entity_service_get_by_id(&client, org2_id, ent_a.id, &fetched, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_NOT_FOUND, "Get Entity A under Org 2 returns NOT_FOUND (cross-org isolation)");
    EXPECT_TRUE(strcmp(err_msg, "Entity not found") == 0, "Generic not found message returned");

    res = entity_service_get_by_id(&client, org1_id, 999999, &fetched, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_NOT_FOUND, "Get nonexistent entity returns NOT_FOUND");

    Entity updated;
    res = entity_service_update(&client, org1_id, user1_id, ent_a.id, "Acme Supply Chain",
                               ENTITY_TYPE_UNKNOWN, &updated, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Update Entity A name succeeds");
    EXPECT_TRUE(strcmp(updated.name, "Acme Supply Chain") == 0, "Updated name persisted");
    EXPECT_TRUE(updated.entity_type == ENTITY_TYPE_VENDOR, "Unchanged entity type preserved");

    res = entity_service_update(&client, org1_id, user1_id, ent_a.id, "Apex Holdings",
                               ENTITY_TYPE_UNKNOWN, &updated, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_CONFLICT, "Update name colliding with existing entity in same org rejected");

    res = entity_service_update(&client, org1_id, user1_id, ent_a.id, NULL,
                               ENTITY_TYPE_PARTNER, &updated, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Update Entity A type to Partner succeeds");
    EXPECT_TRUE(updated.entity_type == ENTITY_TYPE_PARTNER, "Updated type persisted");

    Entity status_ent;
    res = entity_service_update_status(&client, org1_id, user1_id, ent_a.id,
                                      ENTITY_STATUS_INACTIVE, &status_ent, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Deactivate Entity A to Inactive succeeds");
    EXPECT_TRUE(status_ent.status == ENTITY_STATUS_INACTIVE, "Status Inactive confirmed");

    res = entity_service_update_status(&client, org1_id, user1_id, ent_a.id,
                                      ENTITY_STATUS_ACTIVE, &status_ent, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_OK, "Reactivate Entity A to Active succeeds");
    EXPECT_TRUE(status_ent.status == ENTITY_STATUS_ACTIVE, "Status Active confirmed");

    res = entity_service_update_status(&client, org1_id, user1_id, ent_a.id,
                                      ENTITY_STATUS_UNKNOWN, &status_ent, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_INVALID_PARAM, "Invalid status change rejected");

    res = entity_service_update_status(&client, org2_id, user2_id, ent_a.id,
                                      ENTITY_STATUS_INACTIVE, &status_ent, err_msg, sizeof(err_msg));
    EXPECT_TRUE(res == ENTITY_ERR_NOT_FOUND, "Cross-org status update rejected with NOT_FOUND");

    db_client_close(&client);

    printf("\nEntity CRUD Tests Completed. Passed: %d, Failed: %d\n",
           g_tests_passed, g_tests_failed);

    return g_tests_failed == 0 ? 0 : 1;
}
