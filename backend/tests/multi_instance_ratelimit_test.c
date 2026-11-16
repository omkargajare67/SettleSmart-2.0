#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/auth/auth_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    load_env_file(".env");
    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client_a;
    DBClient client_b;

    if (db_client_init(&client_a) != 0 || db_client_connect(&client_a, &db_cfg) != 0) {
        printf("FAILED: client_a connection failed\n");
        return 1;
    }
    if (db_client_init(&client_b) != 0 || db_client_connect(&client_b, &db_cfg) != 0) {
        printf("FAILED: client_b connection failed\n");
        db_client_close(&client_a);
        return 1;
    }

    printf("PASSED: Two independent DBClient connections established (Instance A & Instance B)\n");

    db_client_query(&client_a, "DELETE FROM login_rate_limits;");
    db_client_query(&client_a, "DELETE FROM sessions;");
    db_client_query(&client_a, "DELETE FROM users WHERE email LIKE '%ratelimit%';");
    db_client_query(&client_a, "DELETE FROM organizations WHERE name = 'RateLimit Org';");

    long long org_id = 0;
    long long user_id = 0;
    char err_buf[256];

    if (auth_register_organization(&client_a, "RateLimit Org", "admin@ratelimit.com",
                                   "AdminSecret#2026", &org_id, &user_id,
                                   err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Organization registration failed: %s\n", err_buf);
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: Test organization created with ID: %lld\n", org_id);

    auth_record_failed_attempt_db(&client_a, org_id, "admin@ratelimit.com");
    auth_record_failed_attempt_db(&client_a, org_id, "admin@ratelimit.com");
    auth_record_failed_attempt_db(&client_a, org_id, "admin@ratelimit.com");

    if (auth_is_rate_limited_db(&client_b, org_id, "admin@ratelimit.com") != 0) {
        printf("FAILED: Premature rate limit lockout reported by client_b\n");
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: 3 failed attempts on Instance A does not trigger lockout on Instance B\n");

    auth_record_failed_attempt_db(&client_b, org_id, "admin@ratelimit.com");
    auth_record_failed_attempt_db(&client_b, org_id, "admin@ratelimit.com");

    if (auth_is_rate_limited_db(&client_a, org_id, "admin@ratelimit.com") != 1) {
        printf("FAILED: Instance A failed to recognize cumulative 5-attempt rate limit\n");
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    if (auth_is_rate_limited_db(&client_b, org_id, "admin@ratelimit.com") != 1) {
        printf("FAILED: Instance B failed to recognize cumulative 5-attempt rate limit\n");
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: 5 cumulative failed attempts across Instance A and B enforces lockout on both\n");

    char sess_token[128];
    char csrf_token[128];
    AuthUserInfo uinfo;
    int login_res = auth_login(&client_a, org_id, "admin@ratelimit.com", "AdminSecret#2026",
                               sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                               &uinfo, err_buf, sizeof(err_buf));
    if (login_res != -2 || strstr(err_buf, "Too many failed login attempts") == NULL) {
        printf("FAILED: auth_login on Instance A did not return rate limit lockout (-2): %d, err: %s\n",
               login_res, err_buf);
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: auth_login on Instance A rejected with centralized rate limit lockout\n");

    login_res = auth_login(&client_b, org_id, "admin@ratelimit.com", "AdminSecret#2026",
                           sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                           &uinfo, err_buf, sizeof(err_buf));
    if (login_res != -2 || strstr(err_buf, "Too many failed login attempts") == NULL) {
        printf("FAILED: auth_login on Instance B did not return rate limit lockout (-2): %d, err: %s\n",
               login_res, err_buf);
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: auth_login on Instance B rejected with centralized rate limit lockout\n");

    auth_reset_failed_attempts_db(&client_a, org_id, "admin@ratelimit.com");
    printf("PASSED: Reset rate limit attempts in database via Instance A\n");

    login_res = auth_login(&client_b, org_id, "admin@ratelimit.com", "AdminSecret#2026",
                           sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
                           &uinfo, err_buf, sizeof(err_buf));
    if (login_res != 0) {
        printf("FAILED: Valid login on Instance B failed after reset: %s\n", err_buf);
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: Valid login succeeded on Instance B after centralized reset\n");

    db_client_close(&client_a);
    db_client_close(&client_b);
    printf("\nALL MULTI-INSTANCE CENTRALIZED RATE LIMIT TESTS SUCCEEDED.\n");
    return 0;
}
