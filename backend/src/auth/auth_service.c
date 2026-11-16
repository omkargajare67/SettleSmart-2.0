#include "auth_service.h"
#include "crypto_utils.h"
#include "password.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION g_rate_limit_lock;
static int g_rate_limit_lock_init = 0;
static void rate_limit_lock(void) {
    if (!g_rate_limit_lock_init) {
        InitializeCriticalSection(&g_rate_limit_lock);
        g_rate_limit_lock_init = 1;
    }
    EnterCriticalSection(&g_rate_limit_lock);
}
static void rate_limit_unlock(void) {
    LeaveCriticalSection(&g_rate_limit_lock);
}
#else
#include <pthread.h>
static pthread_mutex_t g_rate_limit_lock = PTHREAD_MUTEX_INITIALIZER;
static void rate_limit_lock(void) {
    pthread_mutex_lock(&g_rate_limit_lock);
}
static void rate_limit_unlock(void) {
    pthread_mutex_unlock(&g_rate_limit_lock);
}
#endif

#define MAX_RATE_LIMIT_ENTRIES 128
#define MAX_FAILED_ATTEMPTS 5
#define RATE_LIMIT_LOCKOUT_SEC 900

typedef struct {
    long long organization_id;
    char email[256];
    int failed_count;
    time_t last_failure;
} RateLimitEntry;

static RateLimitEntry g_rate_limit_table[MAX_RATE_LIMIT_ENTRIES];
static int g_rate_limit_count = 0;

static void set_error(char *err_msg, size_t err_len, const char *msg) {
    if (err_msg && err_len > 0) {
        strncpy(err_msg, msg, err_len - 1);
        err_msg[err_len - 1] = '\0';
    }
}

void auth_record_failed_attempt(long long organization_id, const char *email) {
    if (!email) {
        return;
    }
    rate_limit_lock();
    time_t now = time(NULL);
    for (int i = 0; i < g_rate_limit_count; i++) {
        if (g_rate_limit_table[i].organization_id == organization_id &&
            strcmp(g_rate_limit_table[i].email, email) == 0) {
            if (now - g_rate_limit_table[i].last_failure > RATE_LIMIT_LOCKOUT_SEC) {
                g_rate_limit_table[i].failed_count = 1;
            } else {
                g_rate_limit_table[i].failed_count++;
            }
            g_rate_limit_table[i].last_failure = now;
            rate_limit_unlock();
            return;
        }
    }
    if (g_rate_limit_count < MAX_RATE_LIMIT_ENTRIES) {
        g_rate_limit_table[g_rate_limit_count].organization_id = organization_id;
        strncpy(g_rate_limit_table[g_rate_limit_count].email, email,
                sizeof(g_rate_limit_table[g_rate_limit_count].email) - 1);
        g_rate_limit_table[g_rate_limit_count].email[sizeof(g_rate_limit_table[g_rate_limit_count].email) - 1] = '\0';
        g_rate_limit_table[g_rate_limit_count].failed_count = 1;
        g_rate_limit_table[g_rate_limit_count].last_failure = now;
        g_rate_limit_count++;
    }
    rate_limit_unlock();
}

void auth_reset_failed_attempts(long long organization_id, const char *email) {
    if (!email) {
        return;
    }
    rate_limit_lock();
    for (int i = 0; i < g_rate_limit_count; i++) {
        if (g_rate_limit_table[i].organization_id == organization_id &&
            strcmp(g_rate_limit_table[i].email, email) == 0) {
            g_rate_limit_table[i].failed_count = 0;
            rate_limit_unlock();
            return;
        }
    }
    rate_limit_unlock();
}

int auth_is_rate_limited(long long organization_id, const char *email) {
    if (!email) {
        return 0;
    }
    rate_limit_lock();
    time_t now = time(NULL);
    for (int i = 0; i < g_rate_limit_count; i++) {
        if (g_rate_limit_table[i].organization_id == organization_id &&
            strcmp(g_rate_limit_table[i].email, email) == 0) {
            if (g_rate_limit_table[i].failed_count >= MAX_FAILED_ATTEMPTS) {
                if (now - g_rate_limit_table[i].last_failure < RATE_LIMIT_LOCKOUT_SEC) {
                    rate_limit_unlock();
                    return 1;
                } else {
                    g_rate_limit_table[i].failed_count = 0;
                    rate_limit_unlock();
                    return 0;
                }
            }
            rate_limit_unlock();
            return 0;
        }
    }
    rate_limit_unlock();
    return 0;
}

int auth_is_rate_limited_db(DBClient *client, long long organization_id, const char *email) {
    if (!email) {
        return 0;
    }
    if (!client || !client->connection) {
        return auth_is_rate_limited(organization_id, email);
    }
    char esc_email[512];
    mysql_real_escape_string(client->connection, esc_email, email, (unsigned long)strlen(email));

    char query[768];
    snprintf(query, sizeof(query),
             "SELECT failed_count, TIMESTAMPDIFF(SECOND, last_failed_at, NOW()) AS elapsed_sec "
             "FROM login_rate_limits WHERE organization_id = %lld AND email = '%s';",
             organization_id, esc_email);

    if (db_client_query(client, query) != 0) {
        return auth_is_rate_limited(organization_id, email);
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        return auth_is_rate_limited(organization_id, email);
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        return 0;
    }

    int failed = row[0] ? atoi(row[0]) : 0;
    long long elapsed = row[1] ? atoll(row[1]) : 0;
    db_client_free_result(res);

    if (failed >= 5) {
        if (elapsed < 900) {
            return 1;
        } else {
            char reset_q[512];
            snprintf(reset_q, sizeof(reset_q),
                     "UPDATE login_rate_limits SET failed_count = 0 WHERE organization_id = %lld AND email = '%s';",
                     organization_id, esc_email);
            db_client_query(client, reset_q);
            return 0;
        }
    }
    return 0;
}

void auth_record_failed_attempt_db(DBClient *client, long long organization_id, const char *email) {
    if (!email) {
        return;
    }
    auth_record_failed_attempt(organization_id, email);
    if (!client || !client->connection) {
        return;
    }

    char esc_email[512];
    mysql_real_escape_string(client->connection, esc_email, email, (unsigned long)strlen(email));

    char query[768];
    snprintf(query, sizeof(query),
             "INSERT INTO login_rate_limits (organization_id, email, failed_count, last_failed_at) "
             "VALUES (%lld, '%s', 1, NOW()) "
             "ON DUPLICATE KEY UPDATE "
             "failed_count = IF(TIMESTAMPDIFF(SECOND, last_failed_at, NOW()) > 900, 1, failed_count + 1), "
             "last_failed_at = NOW();",
             organization_id, esc_email);

    db_client_query(client, query);
}

void auth_reset_failed_attempts_db(DBClient *client, long long organization_id, const char *email) {
    if (!email) {
        return;
    }
    auth_reset_failed_attempts(organization_id, email);
    if (!client || !client->connection) {
        return;
    }

    char esc_email[512];
    mysql_real_escape_string(client->connection, esc_email, email, (unsigned long)strlen(email));

    char query[512];
    snprintf(query, sizeof(query),
             "DELETE FROM login_rate_limits WHERE organization_id = %lld AND email = '%s';",
             organization_id, esc_email);

    db_client_query(client, query);
}

static void escape_string(DBClient *client, const char *src, char *dest, size_t dest_len) {
    if (!src || !dest || dest_len == 0) {
        return;
    }
    if (client && client->connection) {
        mysql_real_escape_string(client->connection, dest, src, (unsigned long)strlen(src));
    } else {
        strncpy(dest, src, dest_len - 1);
        dest[dest_len - 1] = '\0';
    }
}

int auth_register_organization(DBClient *client, const char *org_name,
                               const char *admin_email, const char *admin_password,
                               long long *out_org_id, long long *out_user_id,
                               char *err_msg, size_t err_len) {
    if (!client || !org_name || !admin_email || !admin_password) {
        set_error(err_msg, err_len, "Missing required parameters");
        return -1;
    }

    if (strlen(org_name) == 0 || strlen(org_name) > 150) {
        set_error(err_msg, err_len, "Organization name must be between 1 and 150 characters");
        return -1;
    }

    if (strlen(admin_email) == 0 || strlen(admin_email) > 255 || strchr(admin_email, '@') == NULL) {
        set_error(err_msg, err_len, "Invalid administrator email address");
        return -1;
    }

    if (auth_validate_password_length(admin_password) != 0) {
        set_error(err_msg, err_len, "Password must be between 12 and 128 characters");
        return -1;
    }

    char hash[AUTH_HASH_MAX_LEN];
    if (auth_hash_password(admin_password, hash, sizeof(hash)) != 0) {
        set_error(err_msg, err_len, "Failed to hash administrator password");
        return -1;
    }

    if (db_client_begin(client) != 0) {
        set_error(err_msg, err_len, "Failed to start database transaction");
        return -1;
    }

    char esc_org[301];
    escape_string(client, org_name, esc_org, sizeof(esc_org));

    char org_query[512];
    snprintf(org_query, sizeof(org_query),
             "INSERT INTO organizations (name, status) VALUES ('%s', 'Active');", esc_org);

    if (db_client_query(client, org_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to create organization: Name may already exist");
        return -1;
    }

    long long org_id = (long long)db_client_last_insert_id(client);

    char esc_email[513];
    escape_string(client, admin_email, esc_email, sizeof(esc_email));

    char user_query[768];
    snprintf(user_query, sizeof(user_query),
             "INSERT INTO users (organization_id, email, password_hash, role, status) "
             "VALUES (%lld, '%s', '%s', 'Admin', 'Active');",
             org_id, esc_email, hash);

    if (db_client_query(client, user_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to create initial administrator user");
        return -1;
    }

    long long user_id = (long long)db_client_last_insert_id(client);

    if (db_client_commit(client) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to commit registration transaction");
        return -1;
    }

    if (out_org_id) {
        *out_org_id = org_id;
    }
    if (out_user_id) {
        *out_user_id = user_id;
    }

    return 0;
}

int auth_login(DBClient *client, long long organization_id,
               const char *email, const char *password,
               char *out_session_token, size_t sess_len,
               char *out_csrf_token, size_t csrf_len,
               AuthUserInfo *out_user,
               char *err_msg, size_t err_len) {
    if (!client || !email || !password || !out_session_token || !out_csrf_token) {
        set_error(err_msg, err_len, "Invalid organization ID, email or password");
        return -1;
    }

    if (auth_is_rate_limited_db(client, organization_id, email)) {
        set_error(err_msg, err_len, "Too many failed login attempts. Please try again later.");
        return -2;
    }

    char esc_email[513];
    escape_string(client, email, esc_email, sizeof(esc_email));

    char query[768];
    snprintf(query, sizeof(query),
             "SELECT u.id, u.password_hash, u.role, u.status, o.status "
             "FROM users u "
             "JOIN organizations o ON u.organization_id = o.id "
             "WHERE u.organization_id = %lld AND u.email = '%s' "
             "LIMIT 1;",
             organization_id, esc_email);

    if (db_client_query(client, query) != 0) {
        set_error(err_msg, err_len, "Invalid organization ID, email or password");
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_error(err_msg, err_len, "Invalid organization ID, email or password");
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        auth_record_failed_attempt_db(client, organization_id, email);
        set_error(err_msg, err_len, "Invalid organization ID, email or password");
        return -1;
    }

    long long user_id = atoll(row[0]);
    const char *stored_hash = row[1];
    const char *user_role_str = row[2];
    const char *user_status = row[3];
    const char *org_status = row[4];

    if (strcmp(org_status, "Active") != 0 || strcmp(user_status, "Active") != 0) {
        db_client_free_result(res);
        auth_record_failed_attempt_db(client, organization_id, email);
        set_error(err_msg, err_len, "Invalid organization ID, email or password");
        return -1;
    }

    char hash_copy[AUTH_HASH_MAX_LEN];
    strncpy(hash_copy, stored_hash ? stored_hash : "", sizeof(hash_copy) - 1);
    hash_copy[sizeof(hash_copy) - 1] = '\0';

    char role_copy[32];
    strncpy(role_copy, user_role_str ? user_role_str : "", sizeof(role_copy) - 1);
    role_copy[sizeof(role_copy) - 1] = '\0';

    db_client_free_result(res);

    if (auth_verify_password(password, hash_copy) != 0) {
        auth_record_failed_attempt_db(client, organization_id, email);
        set_error(err_msg, err_len, "Invalid organization ID, email or password");
        return -1;
    }

    auth_reset_failed_attempts_db(client, organization_id, email);

    if (session_create(client, organization_id, user_id,
                       out_session_token, sess_len,
                       out_csrf_token, csrf_len) != 0) {
        set_error(err_msg, err_len, "Failed to create session");
        return -1;
    }

    if (out_user) {
        out_user->id = user_id;
        out_user->organization_id = organization_id;
        strncpy(out_user->email, email, sizeof(out_user->email) - 1);
        out_user->email[sizeof(out_user->email) - 1] = '\0';
        out_user->role = auth_role_from_string(role_copy);
        strncpy(out_user->status, "Active", sizeof(out_user->status) - 1);
        out_user->status[sizeof(out_user->status) - 1] = '\0';
    }

    return 0;
}

int auth_change_password(DBClient *client, long long organization_id, long long user_id,
                         const char *old_password, const char *new_password,
                         char *err_msg, size_t err_len) {
    if (!client || !old_password || !new_password) {
        set_error(err_msg, err_len, "Missing password parameters");
        return -1;
    }

    if (auth_validate_password_length(new_password) != 0) {
        set_error(err_msg, err_len, "New password must be between 12 and 128 characters");
        return -1;
    }

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT password_hash FROM users WHERE organization_id = %lld AND id = %lld AND status = 'Active';",
             organization_id, user_id);

    if (db_client_query(client, query) != 0) {
        set_error(err_msg, err_len, "Failed to locate user account");
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_error(err_msg, err_len, "Failed to locate user account");
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        db_client_free_result(res);
        set_error(err_msg, err_len, "User account not found");
        return -1;
    }

    char current_hash[AUTH_HASH_MAX_LEN];
    strncpy(current_hash, row[0], sizeof(current_hash) - 1);
    current_hash[sizeof(current_hash) - 1] = '\0';
    db_client_free_result(res);

    if (auth_verify_password(old_password, current_hash) != 0) {
        set_error(err_msg, err_len, "Current password does not match");
        return -1;
    }

    char new_hash[AUTH_HASH_MAX_LEN];
    if (auth_hash_password(new_password, new_hash, sizeof(new_hash)) != 0) {
        set_error(err_msg, err_len, "Failed to hash new password");
        return -1;
    }

    char update_query[512];
    snprintf(update_query, sizeof(update_query),
             "UPDATE users SET password_hash = '%s' WHERE organization_id = %lld AND id = %lld;",
             new_hash, organization_id, user_id);

    if (db_client_query(client, update_query) != 0) {
        set_error(err_msg, err_len, "Failed to update password");
        return -1;
    }

    session_revoke_all_user(client, organization_id, user_id);
    return 0;
}

int auth_create_user(DBClient *client, long long actor_user_id, UserRole actor_role,
                     long long organization_id, const char *email, const char *password,
                     UserRole role, long long *out_user_id,
                     char *err_msg, size_t err_len) {
    (void)actor_user_id;
    if (!client || !email || !password) {
        set_error(err_msg, err_len, "Missing required parameters");
        return -1;
    }

    if (!auth_can_manage_users(actor_role)) {
        set_error(err_msg, err_len, "Permission denied: Administrator role required to manage users");
        return -1;
    }

    if (strlen(email) == 0 || strlen(email) > 255 || strchr(email, '@') == NULL) {
        set_error(err_msg, err_len, "Invalid email address");
        return -1;
    }

    if (auth_validate_password_length(password) != 0) {
        set_error(err_msg, err_len, "Password must be between 12 and 128 characters");
        return -1;
    }

    const char *role_str = auth_role_to_string(role);
    if (role == ROLE_UNKNOWN) {
        set_error(err_msg, err_len, "Invalid role specified");
        return -1;
    }

    char hash[AUTH_HASH_MAX_LEN];
    if (auth_hash_password(password, hash, sizeof(hash)) != 0) {
        set_error(err_msg, err_len, "Failed to hash user password");
        return -1;
    }

    char esc_email[513];
    escape_string(client, email, esc_email, sizeof(esc_email));

    char query[768];
    snprintf(query, sizeof(query),
             "INSERT INTO users (organization_id, email, password_hash, role, status) "
             "VALUES (%lld, '%s', '%s', '%s', 'Active');",
             organization_id, esc_email, hash, role_str);

    if (db_client_query(client, query) != 0) {
        set_error(err_msg, err_len, "Failed to create user: Email may already exist in organization");
        return -1;
    }

    if (out_user_id) {
        *out_user_id = (long long)db_client_last_insert_id(client);
    }

    return 0;
}

int auth_update_user_status(DBClient *client, long long actor_user_id, UserRole actor_role,
                            long long organization_id, long long target_user_id,
                            const char *new_status,
                            char *err_msg, size_t err_len) {
    (void)actor_user_id;
    if (!client || !new_status) {
        set_error(err_msg, err_len, "Missing parameters");
        return -1;
    }

    if (!auth_can_manage_users(actor_role)) {
        set_error(err_msg, err_len, "Permission denied: Administrator role required to update user status");
        return -1;
    }

    if (strcmp(new_status, "Active") != 0 && strcmp(new_status, "Deactivated") != 0) {
        set_error(err_msg, err_len, "Status must be either 'Active' or 'Deactivated'");
        return -1;
    }

    if (db_client_begin(client) != 0) {
        set_error(err_msg, err_len, "Failed to start database transaction");
        return -1;
    }

    char lock_org_query[256];
    snprintf(lock_org_query, sizeof(lock_org_query),
             "SELECT id FROM organizations WHERE id = %lld FOR UPDATE;",
             organization_id);

    if (db_client_query(client, lock_org_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to lock organization");
        return -1;
    }

    MYSQL_RES *lock_res = db_client_store_result(client);
    if (lock_res) {
        db_client_free_result(lock_res);
    }

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT role, status FROM users WHERE organization_id = %lld AND id = %lld FOR UPDATE;",
             organization_id, target_user_id);

    if (db_client_query(client, query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to query target user");
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to query target user");
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        db_client_rollback(client);
        set_error(err_msg, err_len, "Target user not found");
        return -1;
    }

    char target_role_str[32];
    strncpy(target_role_str, row[0] ? row[0] : "", sizeof(target_role_str) - 1);
    target_role_str[sizeof(target_role_str) - 1] = '\0';

    char target_curr_status[32];
    strncpy(target_curr_status, row[1] ? row[1] : "", sizeof(target_curr_status) - 1);
    target_curr_status[sizeof(target_curr_status) - 1] = '\0';
    db_client_free_result(res);

    if (strcmp(new_status, "Deactivated") == 0 &&
        strcmp(target_role_str, "Admin") == 0 &&
        strcmp(target_curr_status, "Active") == 0) {

        char count_query[256];
        snprintf(count_query, sizeof(count_query),
                 "SELECT COUNT(*) FROM users WHERE organization_id = %lld AND role = 'Admin' AND status = 'Active' AND id <> %lld;",
                 organization_id, target_user_id);

        if (db_client_query(client, count_query) != 0) {
            db_client_rollback(client);
            set_error(err_msg, err_len, "Failed to verify administrator count");
            return -1;
        }

        MYSQL_RES *c_res = db_client_store_result(client);
        int other_active_admins = 0;
        if (c_res) {
            MYSQL_ROW c_row = mysql_fetch_row(c_res);
            if (c_row && c_row[0]) {
                other_active_admins = atoi(c_row[0]);
            }
            db_client_free_result(c_res);
        }

        if (other_active_admins == 0) {
            db_client_rollback(client);
            set_error(err_msg, err_len, "Cannot deactivate the last active administrator of the organization");
            return -2;
        }
    }

    char update_query[256];
    snprintf(update_query, sizeof(update_query),
             "UPDATE users SET status = '%s' WHERE organization_id = %lld AND id = %lld;",
             new_status, organization_id, target_user_id);

    if (db_client_query(client, update_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to update user status");
        return -1;
    }

    if (db_client_commit(client) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to commit status update");
        return -1;
    }

    if (strcmp(new_status, "Deactivated") == 0) {
        session_revoke_all_user(client, organization_id, target_user_id);
    }

    return 0;
}

int auth_update_user_role(DBClient *client, long long actor_user_id, UserRole actor_role,
                          long long organization_id, long long target_user_id,
                          UserRole new_role,
                          char *err_msg, size_t err_len) {
    (void)actor_user_id;
    if (!client) {
        set_error(err_msg, err_len, "Missing database client");
        return -1;
    }

    if (!auth_can_manage_users(actor_role)) {
        set_error(err_msg, err_len, "Permission denied: Administrator role required to update user role");
        return -1;
    }

    if (new_role == ROLE_UNKNOWN) {
        set_error(err_msg, err_len, "Invalid new role specified");
        return -1;
    }

    if (db_client_begin(client) != 0) {
        set_error(err_msg, err_len, "Failed to start database transaction");
        return -1;
    }

    char lock_org_query[256];
    snprintf(lock_org_query, sizeof(lock_org_query),
             "SELECT id FROM organizations WHERE id = %lld FOR UPDATE;",
             organization_id);

    if (db_client_query(client, lock_org_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to lock organization");
        return -1;
    }

    MYSQL_RES *lock_res = db_client_store_result(client);
    if (lock_res) {
        db_client_free_result(lock_res);
    }

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT role, status FROM users WHERE organization_id = %lld AND id = %lld FOR UPDATE;",
             organization_id, target_user_id);

    if (db_client_query(client, query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to query target user");
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to query target user");
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        db_client_rollback(client);
        set_error(err_msg, err_len, "Target user not found");
        return -1;
    }

    char target_role_str[32];
    strncpy(target_role_str, row[0] ? row[0] : "", sizeof(target_role_str) - 1);
    target_role_str[sizeof(target_role_str) - 1] = '\0';

    char target_curr_status[32];
    strncpy(target_curr_status, row[1] ? row[1] : "", sizeof(target_curr_status) - 1);
    target_curr_status[sizeof(target_curr_status) - 1] = '\0';
    db_client_free_result(res);

    if (strcmp(target_role_str, "Admin") == 0 &&
        strcmp(target_curr_status, "Active") == 0 &&
        new_role != ROLE_ADMIN) {

        char count_query[256];
        snprintf(count_query, sizeof(count_query),
                 "SELECT COUNT(*) FROM users WHERE organization_id = %lld AND role = 'Admin' AND status = 'Active' AND id <> %lld;",
                 organization_id, target_user_id);

        if (db_client_query(client, count_query) != 0) {
            db_client_rollback(client);
            set_error(err_msg, err_len, "Failed to verify administrator count");
            return -1;
        }

        MYSQL_RES *c_res = db_client_store_result(client);
        int other_active_admins = 0;
        if (c_res) {
            MYSQL_ROW c_row = mysql_fetch_row(c_res);
            if (c_row && c_row[0]) {
                other_active_admins = atoi(c_row[0]);
            }
            db_client_free_result(c_res);
        }

        if (other_active_admins == 0) {
            db_client_rollback(client);
            set_error(err_msg, err_len, "Cannot demote the last active administrator of the organization");
            return -2;
        }
    }

    const char *new_role_str = auth_role_to_string(new_role);
    char update_query[256];
    snprintf(update_query, sizeof(update_query),
             "UPDATE users SET role = '%s' WHERE organization_id = %lld AND id = %lld;",
             new_role_str, organization_id, target_user_id);

    if (db_client_query(client, update_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to update user role");
        return -1;
    }

    if (db_client_commit(client) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to commit role update");
        return -1;
    }

    return 0;
}

int auth_get_user_by_id(DBClient *client, long long organization_id, long long user_id,
                        AuthUserInfo *out_user) {
    if (!client || !out_user) {
        return -1;
    }

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT id, organization_id, email, role, status FROM users "
             "WHERE organization_id = %lld AND id = %lld LIMIT 1;",
             organization_id, user_id);

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

    out_user->id = atoll(row[0]);
    out_user->organization_id = atoll(row[1]);
    strncpy(out_user->email, row[2] ? row[2] : "", sizeof(out_user->email) - 1);
    out_user->email[sizeof(out_user->email) - 1] = '\0';
    out_user->role = auth_role_from_string(row[3]);
    strncpy(out_user->status, row[4] ? row[4] : "", sizeof(out_user->status) - 1);
    out_user->status[sizeof(out_user->status) - 1] = '\0';

    db_client_free_result(res);
    return 0;
}
