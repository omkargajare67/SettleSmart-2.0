#include "../src/auth/auth_service.h"
#include "../src/auth/crypto_utils.h"
#include "../src/auth/session.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cleanup_test_data(DBClient *client) {
    db_client_query(client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'SessTest%');");
    db_client_query(client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'SessTest%');");
    db_client_query(client, "DELETE FROM organizations WHERE name LIKE 'SessTest%';");
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
    printf("Running Session Management Tests (Group D)...\n");

    long long org_id = 0, user_id = 0;
    char err[256] = {0};

    if (auth_register_organization(&client, "SessTest Org", "admin@sesstest.com", "Password#2026Sess",
                                  &org_id, &user_id, err, sizeof(err)) != 0) {
        printf("FAILED: Registration failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }

    char raw_session[128] = {0};
    char raw_csrf[128] = {0};
    AuthUserInfo user_info;

    if (auth_login(&client, org_id, "admin@sesstest.com", "Password#2026Sess",
                   raw_session, sizeof(raw_session), raw_csrf, sizeof(raw_csrf),
                   &user_info, err, sizeof(err)) != 0) {
        printf("FAILED: Login failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }

    char hash_check_query[512];
    snprintf(hash_check_query, sizeof(hash_check_query),
             "SELECT token_hash, csrf_token_hash FROM sessions WHERE user_id = %lld AND organization_id = %lld;",
             user_id, org_id);
    db_client_query(&client, hash_check_query);
    MYSQL_RES *res = db_client_store_result(&client);
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        printf("FAILED: No session row found in database\n");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }

    if (strcmp(row[0], raw_session) == 0 || strcmp(row[1], raw_csrf) == 0) {
        printf("FAILED: Raw token was stored in database plaintext\n");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }

    char expected_sess_hash[65];
    char expected_csrf_hash[65];
    crypto_sha256_hex(raw_session, strlen(raw_session), expected_sess_hash);
    crypto_sha256_hex(raw_csrf, strlen(raw_csrf), expected_csrf_hash);

    if (strcmp(row[0], expected_sess_hash) != 0 || strcmp(row[1], expected_csrf_hash) != 0) {
        printf("FAILED: Stored hash does not match computed SHA-256 hash\n");
        db_client_free_result(res);
        db_client_close(&client);
        return 1;
    }
    db_client_free_result(res);
    printf("PASSED: Verified only SHA-256 hashes stored in database; raw tokens never persisted\n");

    SessionInfo session;
    int val_res = session_validate(&client, raw_session, &session);
    if (val_res != 0) {
        printf("FAILED: Session validation failed for valid raw session token\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Session validation succeeded for active session\n");

    val_res = session_validate(&client, "tampered_session_token_1234567890abcdef", &session);
    if (val_res == 0) {
        printf("FAILED: Tampered session token was validated\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Tampered session token rejected\n");

    char inact_query[256];
    snprintf(inact_query, sizeof(inact_query),
             "UPDATE sessions SET last_seen_at = DATE_SUB(NOW(), INTERVAL 35 MINUTE) WHERE token_hash = '%s';",
             expected_sess_hash);
    db_client_query(&client, inact_query);

    val_res = session_validate(&client, raw_session, &session);
    if (val_res != -4) {
        printf("FAILED: Inactivity timeout (>30 min) did not invalidate session\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Inactivity timeout (>30 min) invalidated session as expected\n");

    char new_raw_sess[128] = {0};
    char new_raw_csrf[128] = {0};
    auth_login(&client, org_id, "admin@sesstest.com", "Password#2026Sess",
               new_raw_sess, sizeof(new_raw_sess), new_raw_csrf, sizeof(new_raw_csrf),
               &user_info, err, sizeof(err));

    char new_sess_hash[65];
    crypto_sha256_hex(new_raw_sess, strlen(new_raw_sess), new_sess_hash);

    char exp_query[256];
    snprintf(exp_query, sizeof(exp_query),
             "UPDATE sessions SET expires_at = DATE_SUB(NOW(), INTERVAL 1 MINUTE) WHERE token_hash = '%s';",
             new_sess_hash);
    db_client_query(&client, exp_query);

    val_res = session_validate(&client, new_raw_sess, &session);
    if (val_res != -3) {
        printf("FAILED: Absolute expiration (12h) did not invalidate session\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Absolute expiration invalidated session as expected\n");

    char logout_sess[128] = {0};
    char logout_csrf[128] = {0};
    auth_login(&client, org_id, "admin@sesstest.com", "Password#2026Sess",
               logout_sess, sizeof(logout_sess), logout_csrf, sizeof(logout_csrf),
               &user_info, err, sizeof(err));

    if (session_revoke(&client, logout_sess) != 0) {
        printf("FAILED: Session revocation failed\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Session revoked successfully\n");

    val_res = session_validate(&client, logout_sess, &session);
    if (val_res == 0) {
        printf("FAILED: Revoked session was still accepted\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Revoked session immediately rejected\n");

    char pwd_sess[128] = {0};
    char pwd_csrf[128] = {0};
    auth_login(&client, org_id, "admin@sesstest.com", "Password#2026Sess",
               pwd_sess, sizeof(pwd_sess), pwd_csrf, sizeof(pwd_csrf),
               &user_info, err, sizeof(err));

    if (auth_change_password(&client, org_id, user_id, "Password#2026Sess", "NewPassword#2026Sess", err, sizeof(err)) != 0) {
        printf("FAILED: Password change failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Password changed successfully\n");

    val_res = session_validate(&client, pwd_sess, &session);
    if (val_res == 0) {
        printf("FAILED: Old session still valid after password change\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Password change revoked all existing sessions for the user\n");

    cleanup_test_data(&client);
    db_client_close(&client);
    printf("\nALL SESSION MANAGEMENT TESTS SUCCEEDED.\n");
    return 0;
}
