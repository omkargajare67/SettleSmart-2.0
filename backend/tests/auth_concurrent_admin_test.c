#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include "../src/auth/auth_service.h"
#include "../src/auth/authorization.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    DBConfig config;
    long long org_id;
    long long actor_id;
    long long target_id;
    int result_code;
    char err_msg[256];
} ConcurrencyThreadArg;

#ifdef _WIN32
static unsigned __stdcall deactivation_worker(void *arg) {
    ConcurrencyThreadArg *t_arg = (ConcurrencyThreadArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return 0;
    }

    t_arg->result_code = auth_update_user_status(&client, t_arg->actor_id, ROLE_ADMIN,
                                                 t_arg->org_id, t_arg->target_id,
                                                 "Deactivated",
                                                 t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return 0;
}

static unsigned __stdcall demotion_worker(void *arg) {
    ConcurrencyThreadArg *t_arg = (ConcurrencyThreadArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return 0;
    }

    t_arg->result_code = auth_update_user_role(&client, t_arg->actor_id, ROLE_ADMIN,
                                               t_arg->org_id, t_arg->target_id,
                                               ROLE_FINANCE_MANAGER,
                                               t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return 0;
}
#else
static void *deactivation_worker(void *arg) {
    ConcurrencyThreadArg *t_arg = (ConcurrencyThreadArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return NULL;
    }

    t_arg->result_code = auth_update_user_status(&client, t_arg->actor_id, ROLE_ADMIN,
                                                 t_arg->org_id, t_arg->target_id,
                                                 "Deactivated",
                                                 t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return NULL;
}

static void *demotion_worker(void *arg) {
    ConcurrencyThreadArg *t_arg = (ConcurrencyThreadArg *)arg;
    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &t_arg->config) != 0) {
        t_arg->result_code = -999;
        return NULL;
    }

    t_arg->result_code = auth_update_user_role(&client, t_arg->actor_id, ROLE_ADMIN,
                                               t_arg->org_id, t_arg->target_id,
                                               ROLE_FINANCE_MANAGER,
                                               t_arg->err_msg, sizeof(t_arg->err_msg));
    db_client_close(&client);
    return NULL;
}
#endif

static void cleanup_test_data(DBClient *client) {
    db_client_query(client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'ConcurAdminTest%' OR name LIKE 'ConcurDemoteTest%');");
    db_client_query(client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'ConcurAdminTest%' OR name LIKE 'ConcurDemoteTest%');");
    db_client_query(client, "DELETE FROM organizations WHERE name LIKE 'ConcurAdminTest%' OR name LIKE 'ConcurDemoteTest%';");
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
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
        printf("FAILED: Main database connection failed\n");
        return 1;
    }

    cleanup_test_data(&client);
    printf("Running Concurrent Last-Admin Protection Test...\n");

    long long org_id = 0, admin1_id = 0;
    char err[256] = {0};

    if (auth_register_organization(&client, "ConcurAdminTest Org", "admin1@concur.com", "MasterPassword#2026",
                                  &org_id, &admin1_id, err, sizeof(err)) != 0) {
        printf("FAILED: Initial registration failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }

    long long admin2_id = 0;
    if (auth_create_user(&client, admin1_id, ROLE_ADMIN, org_id,
                         "admin2@concur.com", "MasterPassword#2026B",
                         ROLE_ADMIN, &admin2_id, err, sizeof(err)) != 0) {
        printf("FAILED: Creation of second admin failed: %s\n", err);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Created organization with exactly 2 active Admins (Admin 1: %lld, Admin 2: %lld)\n",
           admin1_id, admin2_id);

    ConcurrencyThreadArg arg1;
    memcpy(&arg1.config, &config, sizeof(DBConfig));
    arg1.org_id = org_id;
    arg1.actor_id = admin1_id;
    arg1.target_id = admin1_id;
    arg1.result_code = 0;
    memset(arg1.err_msg, 0, sizeof(arg1.err_msg));

    ConcurrencyThreadArg arg2;
    memcpy(&arg2.config, &config, sizeof(DBConfig));
    arg2.org_id = org_id;
    arg2.actor_id = admin2_id;
    arg2.target_id = admin2_id;
    arg2.result_code = 0;
    memset(arg2.err_msg, 0, sizeof(arg2.err_msg));

#ifdef _WIN32
    HANDLE handles[2];
    handles[0] = (HANDLE)_beginthreadex(NULL, 0, deactivation_worker, &arg1, 0, NULL);
    handles[1] = (HANDLE)_beginthreadex(NULL, 0, deactivation_worker, &arg2, 0, NULL);

    WaitForMultipleObjects(2, handles, TRUE, INFINITE);
    CloseHandle(handles[0]);
    CloseHandle(handles[1]);
#else
    pthread_t threads[2];
    pthread_create(&threads[0], NULL, deactivation_worker, &arg1);
    pthread_create(&threads[1], NULL, deactivation_worker, &arg2);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
#endif

    printf("Thread 1 result code: %d (msg: %s)\n", arg1.result_code, arg1.err_msg);
    printf("Thread 2 result code: %d (msg: %s)\n", arg2.result_code, arg2.err_msg);

    int success_count = 0;
    int rejected_count = 0;

    if (arg1.result_code == 0) success_count++;
    if (arg1.result_code == -2) rejected_count++;

    if (arg2.result_code == 0) success_count++;
    if (arg2.result_code == -2) rejected_count++;

    if (success_count != 1 || rejected_count != 1) {
        printf("FAILED: Expected exactly 1 success and 1 rejection. Got successes=%d, rejections=%d\n",
               success_count, rejected_count);
        cleanup_test_data(&client);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Exactly one concurrent deactivation succeeded and one was rejected\n");

    char count_query[256];
    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM users WHERE organization_id = %lld AND role = 'Admin' AND status = 'Active';",
             org_id);
    db_client_query(&client, count_query);
    MYSQL_RES *res = db_client_store_result(&client);
    MYSQL_ROW row = mysql_fetch_row(res);
    int remaining_active_admins = row ? atoi(row[0]) : 0;
    db_client_free_result(res);

    if (remaining_active_admins != 1) {
        printf("FAILED: Database in inconsistent state! Active admins remaining: %d\n", remaining_active_admins);
        cleanup_test_data(&client);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Database state verified: exactly 1 active administrator remains in organization\n");

    printf("\nRunning Concurrent Admin Role Demotion Test...\n");
    long long demote_org_id = 0, demote_admin1_id = 0;
    if (auth_register_organization(&client, "ConcurDemoteTest Org", "demote1@concur.com", "MasterPassword#2026",
                                  &demote_org_id, &demote_admin1_id, err, sizeof(err)) != 0) {
        printf("FAILED: Demotion test initial registration failed: %s\n", err);
        cleanup_test_data(&client);
        db_client_close(&client);
        return 1;
    }

    long long demote_admin2_id = 0;
    if (auth_create_user(&client, demote_admin1_id, ROLE_ADMIN, demote_org_id,
                         "demote2@concur.com", "MasterPassword#2026B",
                         ROLE_ADMIN, &demote_admin2_id, err, sizeof(err)) != 0) {
        printf("FAILED: Creation of second admin for demotion test failed: %s\n", err);
        cleanup_test_data(&client);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Created organization with exactly 2 active Admins (Admin 1: %lld, Admin 2: %lld)\n",
           demote_admin1_id, demote_admin2_id);

    ConcurrencyThreadArg d_arg1;
    memcpy(&d_arg1.config, &config, sizeof(DBConfig));
    d_arg1.org_id = demote_org_id;
    d_arg1.actor_id = demote_admin1_id;
    d_arg1.target_id = demote_admin1_id;
    d_arg1.result_code = 0;
    memset(d_arg1.err_msg, 0, sizeof(d_arg1.err_msg));

    ConcurrencyThreadArg d_arg2;
    memcpy(&d_arg2.config, &config, sizeof(DBConfig));
    d_arg2.org_id = demote_org_id;
    d_arg2.actor_id = demote_admin2_id;
    d_arg2.target_id = demote_admin2_id;
    d_arg2.result_code = 0;
    memset(d_arg2.err_msg, 0, sizeof(d_arg2.err_msg));

#ifdef _WIN32
    HANDLE d_handles[2];
    d_handles[0] = (HANDLE)_beginthreadex(NULL, 0, demotion_worker, &d_arg1, 0, NULL);
    d_handles[1] = (HANDLE)_beginthreadex(NULL, 0, demotion_worker, &d_arg2, 0, NULL);

    WaitForMultipleObjects(2, d_handles, TRUE, INFINITE);
    CloseHandle(d_handles[0]);
    CloseHandle(d_handles[1]);
#else
    pthread_t d_threads[2];
    pthread_create(&d_threads[0], NULL, demotion_worker, &d_arg1);
    pthread_create(&d_threads[1], NULL, demotion_worker, &d_arg2);

    pthread_join(d_threads[0], NULL);
    pthread_join(d_threads[1], NULL);
#endif

    printf("Demotion Thread 1 result code: %d (msg: %s)\n", d_arg1.result_code, d_arg1.err_msg);
    printf("Demotion Thread 2 result code: %d (msg: %s)\n", d_arg2.result_code, d_arg2.err_msg);

    int d_success_count = 0;
    int d_rejected_count = 0;

    if (d_arg1.result_code == 0) d_success_count++;
    if (d_arg1.result_code == -2) d_rejected_count++;

    if (d_arg2.result_code == 0) d_success_count++;
    if (d_arg2.result_code == -2) d_rejected_count++;

    if (d_success_count != 1 || d_rejected_count != 1) {
        printf("FAILED: Expected exactly 1 demotion success and 1 rejection. Got successes=%d, rejections=%d\n",
               d_success_count, d_rejected_count);
        cleanup_test_data(&client);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Exactly one concurrent demotion succeeded and one was rejected\n");

    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM users WHERE organization_id = %lld AND role = 'Admin' AND status = 'Active';",
             demote_org_id);
    db_client_query(&client, count_query);
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    int demote_remaining_admins = row ? atoi(row[0]) : 0;
    db_client_free_result(res);

    if (demote_remaining_admins != 1) {
        printf("FAILED: Database in inconsistent state! Active admins remaining: %d\n", demote_remaining_admins);
        cleanup_test_data(&client);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Database state verified: exactly 1 active administrator remains after concurrent demotions\n");

    snprintf(count_query, sizeof(count_query),
             "SELECT COUNT(*) FROM users WHERE organization_id = %lld AND role = 'Finance_Manager' AND status = 'Active';",
             demote_org_id);
    db_client_query(&client, count_query);
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    int demoted_to_fm = row ? atoi(row[0]) : 0;
    db_client_free_result(res);

    if (demoted_to_fm != 1) {
        printf("FAILED: Expected exactly 1 demoted Finance Manager. Found: %d\n", demoted_to_fm);
        cleanup_test_data(&client);
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Database state verified: exactly 1 user demoted to Finance Manager\n");

    cleanup_test_data(&client);
    db_client_close(&client);
    printf("\nCONCURRENT LAST-ADMIN PROTECTION TEST SUCCEEDED.\n");
    return 0;
}
