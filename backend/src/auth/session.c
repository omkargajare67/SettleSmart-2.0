#include "session.h"
#include "crypto_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int session_create(DBClient *client, long long organization_id, long long user_id,
                   char *raw_session_token, size_t sess_len,
                   char *raw_csrf_token, size_t csrf_len) {
    if (!client || !raw_session_token || !raw_csrf_token || sess_len < 65 || csrf_len < 65) {
        return -1;
    }

    if (crypto_generate_token(raw_session_token, sess_len) != 0) {
        return -1;
    }
    if (crypto_generate_token(raw_csrf_token, csrf_len) != 0) {
        return -1;
    }

    char sess_hash[65];
    char csrf_hash[65];
    crypto_sha256_hex(raw_session_token, strlen(raw_session_token), sess_hash);
    crypto_sha256_hex(raw_csrf_token, strlen(raw_csrf_token), csrf_hash);

    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO sessions (organization_id, user_id, token_hash, csrf_token_hash, expires_at, last_seen_at) "
             "VALUES (%lld, %lld, '%s', '%s', DATE_ADD(NOW(), INTERVAL 12 HOUR), NOW());",
             organization_id, user_id, sess_hash, csrf_hash);

    if (db_client_query(client, query) != 0) {
        return -1;
    }

    return 0;
}

int session_validate(DBClient *client, const char *raw_session_token, SessionInfo *out_session) {
    if (!client || !raw_session_token || !out_session) {
        return -1;
    }

    char sess_hash[65];
    crypto_sha256_hex(raw_session_token, strlen(raw_session_token), sess_hash);

    char query[800];
    snprintf(query, sizeof(query),
             "SELECT s.id, s.organization_id, s.user_id, u.email, u.role, u.status, s.token_hash, s.csrf_token_hash, "
             "TIMESTAMPDIFF(SECOND, s.last_seen_at, NOW()) AS inactive_sec, "
             "TIMESTAMPDIFF(SECOND, NOW(), s.expires_at) AS remaining_sec, "
             "s.revoked_at, IFNULL(s.prev_csrf_token_hash, '') "
             "FROM sessions s "
             "JOIN users u ON s.organization_id = u.organization_id AND s.user_id = u.id "
             "JOIN organizations o ON s.organization_id = o.id "
             "WHERE s.token_hash = '%s' "
             "AND o.status = 'Active' "
             "AND u.status = 'Active' "
             "LIMIT 1;",
             sess_hash);

    if (db_client_query(client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        return -1;
    }

    if (row[10] != NULL) {
        db_client_free_result(res);
        return -2;
    }

    long long remaining_sec = row[9] ? atoll(row[9]) : 0;
    if (remaining_sec <= 0) {
        db_client_free_result(res);
        return -3;
    }

    long long inactive_sec = row[8] ? atoll(row[8]) : 0;
    if (inactive_sec > 1800) {
        long long sess_id = atoll(row[0]);
        db_client_free_result(res);
        char revoke_query[128];
        snprintf(revoke_query, sizeof(revoke_query),
                 "UPDATE sessions SET revoked_at = NOW() WHERE id = %lld;", sess_id);
        db_client_query(client, revoke_query);
        return -4;
    }

    out_session->session_id = atoll(row[0]);
    out_session->organization_id = atoll(row[1]);
    out_session->user_id = atoll(row[2]);
    strncpy(out_session->user_email, row[3] ? row[3] : "", sizeof(out_session->user_email) - 1);
    out_session->user_email[sizeof(out_session->user_email) - 1] = '\0';
    out_session->user_role = auth_role_from_string(row[4]);
    strncpy(out_session->token_hash, row[6] ? row[6] : "", sizeof(out_session->token_hash) - 1);
    out_session->token_hash[sizeof(out_session->token_hash) - 1] = '\0';
    strncpy(out_session->csrf_token_hash, row[7] ? row[7] : "", sizeof(out_session->csrf_token_hash) - 1);
    out_session->csrf_token_hash[sizeof(out_session->csrf_token_hash) - 1] = '\0';
    strncpy(out_session->prev_csrf_token_hash, row[11] ? row[11] : "", sizeof(out_session->prev_csrf_token_hash) - 1);
    out_session->prev_csrf_token_hash[sizeof(out_session->prev_csrf_token_hash) - 1] = '\0';

    db_client_free_result(res);

    char touch_query[128];
    snprintf(touch_query, sizeof(touch_query),
             "UPDATE sessions SET last_seen_at = NOW() WHERE id = %lld;", out_session->session_id);
    db_client_query(client, touch_query);

    return 0;
}

int session_validate_csrf(const SessionInfo *session, const char *raw_csrf_token) {
    if (!session || !raw_csrf_token) {
        return 0;
    }

    char computed_hash[65];
    crypto_sha256_hex(raw_csrf_token, strlen(raw_csrf_token), computed_hash);

    if (crypto_constant_time_equals(computed_hash, session->csrf_token_hash)) {
        return 1;
    }

    if (session->prev_csrf_token_hash[0] != '\0' &&
        crypto_constant_time_equals(computed_hash, session->prev_csrf_token_hash)) {
        return 1;
    }

    return 0;
}

int session_rotate_csrf_token(DBClient *client, long long session_id, char *out_csrf_token, size_t max_len) {
    if (!client || !out_csrf_token || max_len < 65) {
        return -1;
    }

    if (crypto_generate_token(out_csrf_token, max_len) != 0) {
        return -1;
    }

    char csrf_hash[65];
    crypto_sha256_hex(out_csrf_token, strlen(out_csrf_token), csrf_hash);

    char query[384];
    snprintf(query, sizeof(query),
             "UPDATE sessions SET prev_csrf_token_hash = csrf_token_hash, csrf_token_hash = '%s' WHERE id = %lld;",
             csrf_hash, session_id);

    return db_client_query(client, query);
}

int session_revoke(DBClient *client, const char *raw_session_token) {
    if (!client || !raw_session_token) {
        return -1;
    }

    char sess_hash[65];
    crypto_sha256_hex(raw_session_token, strlen(raw_session_token), sess_hash);

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE sessions SET revoked_at = NOW() WHERE token_hash = '%s' AND revoked_at IS NULL;",
             sess_hash);

    return db_client_query(client, query);
}

int session_revoke_all_user(DBClient *client, long long organization_id, long long user_id) {
    if (!client) {
        return -1;
    }

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE sessions SET revoked_at = NOW() WHERE organization_id = %lld AND user_id = %lld AND revoked_at IS NULL;",
             organization_id, user_id);

    return db_client_query(client, query);
}

int session_clean_expired(DBClient *client) {
    if (!client) {
        return -1;
    }

    const char *query = "DELETE FROM sessions WHERE expires_at < NOW() OR revoked_at IS NOT NULL;";
    return db_client_query(client, query);
}
