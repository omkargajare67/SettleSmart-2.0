#include "../src/auth/auth_service.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cleanup_test_data(DBClient *client) {
    db_client_query(client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'RegTest%');");
    db_client_query(client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'RegTest%');");
    db_client_query(client, "DELETE FROM organizations WHERE name LIKE 'RegTest%';");
}

int main(int argc, char **argv) {
    const char *env_file = argc > 1 ? argv[1] : ".env";
    load_env_file(env_file);

    DBConfig config;
    config.host = get_env_value("DB_HOST", "127.0.0.1");
    config.port = get_env_int("DB_PORT", 3307);
    config.database = get_env_value("DB_NAME", "settlesmart_test");
    config.user = get_env_value("DB_USER", "settlesmart_user");
    config.password = get_env_value("DB_PASSWORD", "dev_password_123");
    config.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &config) != 0) {
        printf("FAILED: Database connection failed\n");
        return 1;
    }

    cleanup_test_data(&client);
    printf("Running Registration and Onboarding Tests (Groups A & H)...\n");

    long long org_id = 0;
    long long user_id = 0;
    char err_msg[256] = {0};

    int res = auth_register_organization(&client, "RegTest Corporation Alpha",
                                         "admin@alphacorp.com", "MasterPassword#2026",
                                         &org_id, &user_id, err_msg, sizeof(err_msg));
    if (res != 0) {
        printf("FAILED: Valid registration failed: %s\n", err_msg);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Valid registration succeeded with Org ID: %lld, Admin User ID: %lld\n", org_id, user_id);

    AuthUserInfo user_info;
    if (auth_get_user_by_id(&client, org_id, user_id, &user_info) != 0) {
        printf("FAILED: Failed to fetch created admin user\n");
        db_client_close(&client);
        return 1;
    }

    if (user_info.role != ROLE_ADMIN || strcmp(user_info.status, "Active") != 0) {
        printf("FAILED: Initial user role is not Admin or status is not Active\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Verified initial user has role Admin and status Active\n");

    res = auth_register_organization(&client, "", "admin@empty.com", "ValidPass#12345",
                                     &org_id, &user_id, err_msg, sizeof(err_msg));
    if (res == 0) {
        printf("FAILED: Empty organization name was accepted\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Empty organization name rejected: %s\n", err_msg);

    res = auth_register_organization(&client, "RegTest Bad Email", "invalid-email-string", "ValidPass#12345",
                                     &org_id, &user_id, err_msg, sizeof(err_msg));
    if (res == 0) {
        printf("FAILED: Invalid email format was accepted\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Invalid email format rejected: %s\n", err_msg);

    res = auth_register_organization(&client, "RegTest Short Pwd", "admin@short.com", "short1!",
                                     &org_id, &user_id, err_msg, sizeof(err_msg));
    if (res == 0) {
        printf("FAILED: Password shorter than 12 chars was accepted\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Password shorter than 12 chars rejected: %s\n", err_msg);

    res = auth_register_organization(&client, "RegTest Corporation Alpha",
                                     "another_admin@alphacorp.com", "MasterPassword#2026",
                                     &org_id, &user_id, err_msg, sizeof(err_msg));
    if (res == 0) {
        printf("FAILED: Duplicate organization name was accepted\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Duplicate organization name rejected: %s\n", err_msg);

    db_client_begin(&client);
    db_client_query(&client, "INSERT INTO organizations (name, status) VALUES ('RegTest Rollback Org', 'Active');");
    long long rollback_org_id = (long long)db_client_last_insert_id(&client);
    db_client_rollback(&client);

    char check_query[256];
    snprintf(check_query, sizeof(check_query),
             "SELECT COUNT(*) FROM organizations WHERE id = %lld;", rollback_org_id);
    db_client_query(&client, check_query);
    MYSQL_RES *res_set = db_client_store_result(&client);
    MYSQL_ROW row = mysql_fetch_row(res_set);
    if (!row || strcmp(row[0], "0") != 0) {
        printf("FAILED: Rolled back organization persisted in database\n");
        db_client_free_result(res_set);
        db_client_close(&client);
        return 1;
    }
    db_client_free_result(res_set);
    printf("PASSED: Atomic rollback verified: uncommitted organization record does not persist\n");

    cleanup_test_data(&client);
    db_client_close(&client);
    printf("\nALL REGISTRATION AND ONBOARDING TESTS SUCCEEDED.\n");
    return 0;
}
