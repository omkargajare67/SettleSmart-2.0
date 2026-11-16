#include "../src/auth/auth_service.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cleanup_test_data(DBClient *client) {
    db_client_query(client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'LoginTest%');");
    db_client_query(client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'LoginTest%');");
    db_client_query(client, "DELETE FROM organizations WHERE name LIKE 'LoginTest%';");
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
    printf("Running Login and Authentication Tests (Group C)...\n");

    long long org1_id = 0, user1_id = 0;
    long long org2_id = 0, user2_id = 0;
    char err[256] = {0};

    if (auth_register_organization(&client, "LoginTest Org One", "admin@orgone.com", "PasswordOne#2026",
                                  &org1_id, &user1_id, err, sizeof(err)) != 0) {
        printf("FAILED: Registration Org 1 failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }

    if (auth_register_organization(&client, "LoginTest Org Two", "admin@orgtwo.com", "PasswordTwo#2026",
                                  &org2_id, &user2_id, err, sizeof(err)) != 0) {
        printf("FAILED: Registration Org 2 failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }

    char sess_token[128] = {0};
    char csrf_token[128] = {0};
    AuthUserInfo user_info;

    int res = auth_login(&client, org1_id, "admin@orgone.com", "PasswordOne#2026",
                         sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                         &user_info, err, sizeof(err));
    if (res != 0) {
        printf("FAILED: Valid login failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Valid login succeeded. Session token len: %zu, CSRF token len: %zu\n",
           strlen(sess_token), strlen(csrf_token));

    res = auth_login(&client, org1_id, "admin@orgone.com", "WrongPassword#999",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res == 0 || strcmp(err, "Invalid organization ID, email or password") != 0) {
        printf("FAILED: Incorrect password did not produce expected generic error. Got: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Incorrect password rejected with generic error: %s\n", err);

    res = auth_login(&client, org1_id, "nonexistent@orgone.com", "PasswordOne#2026",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res == 0 || strcmp(err, "Invalid organization ID, email or password") != 0) {
        printf("FAILED: Nonexistent email did not produce generic error\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Nonexistent email rejected with generic error\n");

    res = auth_login(&client, 999999, "admin@orgone.com", "PasswordOne#2026",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res == 0 || strcmp(err, "Invalid organization ID, email or password") != 0) {
        printf("FAILED: Nonexistent org_id did not produce generic error\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Nonexistent org ID rejected with generic error\n");

    res = auth_login(&client, org1_id, "admin@orgtwo.com", "PasswordTwo#2026",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res == 0 || strcmp(err, "Invalid organization ID, email or password") != 0) {
        printf("FAILED: Cross-organization email login did not produce generic error\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Cross-organization email rejected with generic error\n");

    char deact_query[256];
    snprintf(deact_query, sizeof(deact_query),
             "UPDATE users SET status = 'Deactivated' WHERE organization_id = %lld AND id = %lld;",
             org1_id, user1_id);
    db_client_query(&client, deact_query);

    res = auth_login(&client, org1_id, "admin@orgone.com", "PasswordOne#2026",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res == 0 || strcmp(err, "Invalid organization ID, email or password") != 0) {
        printf("FAILED: Deactivated user login did not produce generic error\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Deactivated user login rejected with generic error\n");

    snprintf(deact_query, sizeof(deact_query),
             "UPDATE users SET status = 'Active' WHERE organization_id = %lld AND id = %lld;",
             org1_id, user1_id);
    db_client_query(&client, deact_query);

    char susp_query[256];
    snprintf(susp_query, sizeof(susp_query),
             "UPDATE organizations SET status = 'Suspended' WHERE id = %lld;", org1_id);
    db_client_query(&client, susp_query);

    res = auth_login(&client, org1_id, "admin@orgone.com", "PasswordOne#2026",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res == 0 || strcmp(err, "Invalid organization ID, email or password") != 0) {
        printf("FAILED: Suspended organization login did not produce generic error\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Suspended organization login rejected with generic error\n");

    snprintf(susp_query, sizeof(susp_query),
             "UPDATE organizations SET status = 'Active' WHERE id = %lld;", org1_id);
    db_client_query(&client, susp_query);

    for (int i = 0; i < 4; i++) {
        auth_login(&client, org1_id, "admin@orgone.com", "BadPass123456",
                   sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                   &user_info, err, sizeof(err));
    }
    res = auth_login(&client, org1_id, "admin@orgone.com", "BadPass123456",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res != -2) {
        printf("FAILED: Rate limit lockout failed to trigger after 5 attempts\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Rate limiting triggered successfully: %s\n", err);

    auth_reset_failed_attempts_db(&client, org1_id, "admin@orgone.com");
    res = auth_login(&client, org1_id, "admin@orgone.com", "PasswordOne#2026",
                     sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                     &user_info, err, sizeof(err));
    if (res != 0) {
        printf("FAILED: Login failed after rate limit reset: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Successful login works cleanly after rate limit reset\n");

    cleanup_test_data(&client);
    db_client_close(&client);
    printf("\nALL LOGIN AND AUTHENTICATION TESTS SUCCEEDED.\n");
    return 0;
}
