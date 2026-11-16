#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#endif

#include "../src/auth/auth_service.h"
#include "../src/auth/session.h"
#include "../src/auth/crypto_utils.h"
#include "../src/http/http_server.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_run_ep_server = 1;

#ifdef _WIN32
static unsigned __stdcall ep_server_thread_func(void *arg) {
    (void)arg;
    while (g_run_ep_server) {
        http_server_poll(50);
    }
    return 0;
}
#else
static void *ep_server_thread_func(void *arg) {
    (void)arg;
    while (g_run_ep_server) {
        http_server_poll(50);
    }
    return NULL;
}
#endif

static int send_http(int port, const char *req, char *resp_out, size_t max_resp) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

#ifdef _WIN32
    DWORD timeout = 2500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((unsigned short)port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }

    send(sock, req, (int)strlen(req), 0);

    size_t total = 0;
    while (total < max_resp - 1) {
        int r = recv(sock, resp_out + total, (int)(max_resp - 1 - total), 0);
        if (r <= 0) {
            break;
        }
        total += (size_t)r;
        if (strstr(resp_out, "\r\n\r\n") && (strstr(resp_out, "}\n") || strstr(resp_out, "}"))) {
            break;
        }
    }
    resp_out[total] = '\0';

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return 0;
}

static void cleanup_test_data(DBClient *client) {
    db_client_query(client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'HttpEpTest%');");
    db_client_query(client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'HttpEpTest%');");
    db_client_query(client, "DELETE FROM organizations WHERE name LIKE 'HttpEpTest%';");
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
    DBClient srv_client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &config) != 0 ||
        db_client_init(&srv_client) != 0 || db_client_connect(&srv_client, &config) != 0) {
        printf("FAILED: Database connection failed\n");
        return 1;
    }

    cleanup_test_data(&client);
    printf("Running Full HTTP Endpoints Integration Test Suite...\n");

    HttpServerConfig server_cfg;
    server_cfg.port = 18081;
    strncpy(server_cfg.allowed_origins, "http://localhost:3000, https://app.settlesmart.com", sizeof(server_cfg.allowed_origins) - 1);
    server_cfg.allowed_origins[sizeof(server_cfg.allowed_origins) - 1] = '\0';
    strncpy(server_cfg.app_env, "development", sizeof(server_cfg.app_env) - 1);
    server_cfg.app_env[sizeof(server_cfg.app_env) - 1] = '\0';
    server_cfg.cookie_secure = 0;
    server_cfg.enable_public_registration = 1;
    server_cfg.db_client = &srv_client;

    mg_log_set(MG_LL_NONE);
    if (http_server_start(&server_cfg) != 0) {
        printf("FAILED: Failed to start HTTP server on port 18081\n");
        db_client_close(&client);
        return 1;
    }

#ifdef _WIN32
    uintptr_t thread_handle = _beginthreadex(NULL, 0, ep_server_thread_func, NULL, 0, NULL);
    Sleep(150);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, ep_server_thread_func, NULL);
    usleep(150000);
#endif

    char resp[4096];

    const char *bad_reg_body = "{\"organization_name\":\"HttpEpTest Alpha\",\"admin_email\":\"invalid-email\",\"admin_password\":\"short\"}";
    char bad_reg_req[512];
    snprintf(bad_reg_req, sizeof(bad_reg_req),
             "POST /api/auth/register-organization HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(bad_reg_body), bad_reg_body);

    if (send_http(18081, bad_reg_req, resp, sizeof(resp)) != 0 || strstr(resp, "400") == NULL) {
        printf("FAILED: POST /api/auth/register-organization bad input was not rejected with 400\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 1. POST /api/auth/register-organization rejected invalid input (400 Bad Request)\n");

    const char *good_reg_body = "{\"organization_name\":\"HttpEpTest Alpha\",\"admin_email\":\"admin@eptest.com\",\"admin_password\":\"MasterHttpPassword#2026\"}";
    char good_reg_req[512];
    snprintf(good_reg_req, sizeof(good_reg_req),
             "POST /api/auth/register-organization HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(good_reg_body), good_reg_body);

    if (send_http(18081, good_reg_req, resp, sizeof(resp)) != 0 || strstr(resp, "201") == NULL ||
        strstr(resp, "\"organization_id\"") == NULL) {
        printf("FAILED: POST /api/auth/register-organization valid registration failed. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 1. POST /api/auth/register-organization created organization (201 Created)\n");

    char q_org[256];
    snprintf(q_org, sizeof(q_org), "SELECT id FROM organizations WHERE name = 'HttpEpTest Alpha';");
    db_client_query(&client, q_org);
    MYSQL_RES *res = db_client_store_result(&client);
    if (!res) {
        printf("FAILED: db_client_store_result returned NULL\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&srv_client);
        db_client_close(&client);
        return 1;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row || !row[0]) {
        printf("FAILED: Organization row not found\n");
        db_client_free_result(res);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&srv_client);
        db_client_close(&client);
        return 1;
    }
    long long org_id = atoll(row[0]);
    db_client_free_result(res);

    char bad_login_body[256];
    snprintf(bad_login_body, sizeof(bad_login_body),
             "{\"organization_id\":%lld,\"email\":\"admin@eptest.com\",\"password\":\"WrongPassword#999\"}",
             org_id);
    char bad_login_req[512];
    snprintf(bad_login_req, sizeof(bad_login_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(bad_login_body), bad_login_body);

    if (send_http(18081, bad_login_req, resp, sizeof(resp)) != 0 || strstr(resp, "401") == NULL ||
        strstr(resp, "Invalid organization ID, email or password") == NULL) {
        printf("FAILED: POST /api/auth/login wrong password did not return 401 with generic error\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 2. POST /api/auth/login rejected incorrect credentials with generic 401\n");

    char good_login_body[256];
    snprintf(good_login_body, sizeof(good_login_body),
             "{\"organization_id\":%lld,\"email\":\"admin@eptest.com\",\"password\":\"MasterHttpPassword#2026\"}",
             org_id);
    char good_login_req[512];
    snprintf(good_login_req, sizeof(good_login_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(good_login_body), good_login_body);

    if (send_http(18081, good_login_req, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL ||
        strstr(resp, "Set-Cookie: session_token=") == NULL || strstr(resp, "\"csrf_token\"") == NULL) {
        printf("FAILED: POST /api/auth/login failed. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 2. POST /api/auth/login succeeded (200 OK), issued session cookie and CSRF token\n");

    char session_cookie[128] = {0};
    char *c_pos = strstr(resp, "session_token=");
    if (c_pos) {
        char *end = strchr(c_pos, ';');
        if (end) {
            size_t len = (size_t)(end - c_pos);
            memcpy(session_cookie, c_pos, len);
            session_cookie[len] = '\0';
        }
    }

    char csrf_tok[128] = {0};
    char *csrf_pos = strstr(resp, "\"csrf_token\":\"");
    if (csrf_pos) {
        char *start = csrf_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(csrf_tok, start, len);
            csrf_tok[len] = '\0';
        }
    }

    const char *unauth_me =
        "GET /api/auth/me HTTP/1.1\r\n"
        "Host: 127.0.0.1:18081\r\n"
        "Connection: close\r\n\r\n";

    if (send_http(18081, unauth_me, resp, sizeof(resp)) != 0 || strstr(resp, "401") == NULL) {
        printf("FAILED: GET /api/auth/me without cookie was not rejected with 401\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 3. GET /api/auth/me without session cookie returned 401 Unauthorized\n");

    char auth_me[512];
    snprintf(auth_me, sizeof(auth_me),
             "GET /api/auth/me HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie);

    if (send_http(18081, auth_me, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL ||
        strstr(resp, "\"email\":\"admin@eptest.com\"") == NULL || strstr(resp, "\"role\":\"Admin\"") == NULL) {
        printf("FAILED: GET /api/auth/me with session cookie failed. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 3. GET /api/auth/me with session cookie returned 200 OK and user details\n");

    const char *user_body = "{\"email\":\"fin@eptest.com\",\"password\":\"FinPassword#2026\",\"role\":\"Finance_Manager\"}";
    char create_user_no_csrf[512];
    snprintf(create_user_no_csrf, sizeof(create_user_no_csrf),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             session_cookie, strlen(user_body), user_body);

    if (send_http(18081, create_user_no_csrf, resp, sizeof(resp)) != 0 || strstr(resp, "403") == NULL) {
        printf("FAILED: POST /api/organization/users without CSRF header was not rejected with 403\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 4. POST /api/organization/users without CSRF header returned 403 Forbidden\n");

    char create_user_good[512];
    snprintf(create_user_good, sizeof(create_user_good),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             session_cookie, csrf_tok, strlen(user_body), user_body);

    if (send_http(18081, create_user_good, resp, sizeof(resp)) != 0 || strstr(resp, "201") == NULL ||
        strstr(resp, "\"user_id\"") == NULL) {
        printf("FAILED: POST /api/organization/users with CSRF failed. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 4. POST /api/organization/users with CSRF header created Finance Manager (201 Created)\n");

    char *u_pos = strstr(resp, "\"user_id\":");
    long long created_user_id = u_pos ? atoll(u_pos + 10) : 0;

    char list_users[512];
    snprintf(list_users, sizeof(list_users),
             "GET /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie);

    if (send_http(18081, list_users, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL ||
        strstr(resp, "\"email\":\"admin@eptest.com\"") == NULL || strstr(resp, "\"email\":\"fin@eptest.com\"") == NULL) {
        printf("FAILED: GET /api/organization/users failed. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 5. GET /api/organization/users listed organization users (200 OK)\n");

    char patch_body[128];
    snprintf(patch_body, sizeof(patch_body), "{\"status\":\"Deactivated\"}");
    char patch_req[512];
    snprintf(patch_req, sizeof(patch_req),
             "PATCH /api/organization/users/%lld HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             created_user_id, session_cookie, csrf_tok, strlen(patch_body), patch_body);

    if (send_http(18081, patch_req, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL) {
        printf("FAILED: PATCH /api/organization/users/:id failed. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 6. PATCH /api/organization/users/:id deactivated user (200 OK)\n");

    const char *pwd_body = "{\"old_password\":\"MasterHttpPassword#2026\",\"new_password\":\"NewMasterHttpPassword#2026\"}";
    char chg_pwd_req[512];
    snprintf(chg_pwd_req, sizeof(chg_pwd_req),
             "POST /api/auth/change-password HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             session_cookie, csrf_tok, strlen(pwd_body), pwd_body);

    if (send_http(18081, chg_pwd_req, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL) {
        printf("FAILED: POST /api/auth/change-password failed. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 7. POST /api/auth/change-password changed password successfully (200 OK)\n");

    char new_login_body[256];
    snprintf(new_login_body, sizeof(new_login_body),
             "{\"organization_id\":%lld,\"email\":\"admin@eptest.com\",\"password\":\"NewMasterHttpPassword#2026\"}",
             org_id);
    char new_login_req[512];
    snprintf(new_login_req, sizeof(new_login_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(new_login_body), new_login_body);

    if (send_http(18081, new_login_req, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL) {
        printf("FAILED: Login with new password failed\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }

    c_pos = strstr(resp, "session_token=");
    if (c_pos) {
        char *end = strchr(c_pos, ';');
        if (end) {
            size_t len = (size_t)(end - c_pos);
            memcpy(session_cookie, c_pos, len);
            session_cookie[len] = '\0';
        }
    }

    csrf_pos = strstr(resp, "\"csrf_token\":\"");
    if (csrf_pos) {
        char *start = csrf_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(csrf_tok, start, len);
            csrf_tok[len] = '\0';
        }
    }

    char logout_req[512];
    snprintf(logout_req, sizeof(logout_req),
             "POST /api/auth/logout HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie, csrf_tok);

    if (send_http(18081, logout_req, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL ||
        strstr(resp, "Expires=Thu, 01 Jan 1970") == NULL) {
        printf("FAILED: POST /api/auth/logout failed or did not clear cookie\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: 8. POST /api/auth/logout revoked session and expired cookie (200 OK)\n");

    if (send_http(18081, auth_me, resp, sizeof(resp)) != 0 || strstr(resp, "401") == NULL) {
        printf("FAILED: Revoked session cookie still accepted\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Verified revoked session cookie is rejected on subsequent requests (401 Unauthorized)\n");

    const char *reg_b_body = "{\"organization_name\":\"HttpEpTest Beta\",\"admin_email\":\"admin@beta.com\",\"admin_password\":\"MasterBetaPassword#2026\"}";
    char reg_b_req[512];
    snprintf(reg_b_req, sizeof(reg_b_req),
             "POST /api/auth/register-organization HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(reg_b_body), reg_b_body);

    send_http(18081, reg_b_req, resp, sizeof(resp));

    snprintf(q_org, sizeof(q_org), "SELECT id FROM organizations WHERE name = 'HttpEpTest Beta';");
    db_client_query(&client, q_org);
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    long long org_b_id = atoll(row[0]);
    db_client_free_result(res);

    char login_b_body[256];
    snprintf(login_b_body, sizeof(login_b_body),
             "{\"organization_id\":%lld,\"email\":\"admin@beta.com\",\"password\":\"MasterBetaPassword#2026\"}",
             org_b_id);
    char login_b_req[512];
    snprintf(login_b_req, sizeof(login_b_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(login_b_body), login_b_body);

    send_http(18081, login_b_req, resp, sizeof(resp));

    char b_sess[128] = {0};
    c_pos = strstr(resp, "session_token=");
    if (c_pos) {
        char *end = strchr(c_pos, ';');
        if (end) {
            size_t len = (size_t)(end - c_pos);
            memcpy(b_sess, c_pos, len);
            b_sess[len] = '\0';
        }
    }

    char b_csrf[128] = {0};
    csrf_pos = strstr(resp, "\"csrf_token\":\"");
    if (csrf_pos) {
        char *start = csrf_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(b_csrf, start, len);
            b_csrf[len] = '\0';
        }
    }

    char cross_patch[512];
    snprintf(cross_patch, sizeof(cross_patch),
             "PATCH /api/organization/users/%lld HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             created_user_id, b_sess, b_csrf, strlen(patch_body), patch_body);

    if (send_http(18081, cross_patch, resp, sizeof(resp)) != 0 || strstr(resp, "404") == NULL ||
        strstr(resp, "User not found") == NULL) {
        printf("FAILED: Cross-organization user update was not rejected with 404 User not found. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Verified HTTP organization isolation: Org B admin cannot patch Org A user (404 User not found)\n");

    char nonexist_patch[512];
    snprintf(nonexist_patch, sizeof(nonexist_patch),
             "PATCH /api/organization/users/999999 HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             b_sess, b_csrf, strlen(patch_body), patch_body);

    if (send_http(18081, nonexist_patch, resp, sizeof(resp)) != 0 || strstr(resp, "404") == NULL ||
        strstr(resp, "User not found") == NULL) {
        printf("FAILED: Nonexistent user update was not rejected with 404 User not found\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: API response consistency verified: nonexistent user returns identical 404 User not found\n");

    char admin_a_login[512];
    snprintf(admin_a_login, sizeof(admin_a_login),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             strlen(new_login_body), new_login_body);

    send_http(18081, admin_a_login, resp, sizeof(resp));
    char a_sess[128] = {0};
    c_pos = strstr(resp, "session_token=");
    if (c_pos) {
        char *end = strchr(c_pos, ';');
        if (end) {
            size_t len = (size_t)(end - c_pos);
            memcpy(a_sess, c_pos, len);
            a_sess[len] = '\0';
        }
    }
    char a_csrf[128] = {0};
    csrf_pos = strstr(resp, "\"csrf_token\":\"");
    if (csrf_pos) {
        char *start = csrf_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(a_csrf, start, len);
            a_csrf[len] = '\0';
        }
    }

    const char *add_role_user_body = "{\"email\":\"role_target@eptest.com\",\"password\":\"RoleTargetPass#2026\",\"role\":\"Viewer\"}";
    char add_role_user_req[512];
    snprintf(add_role_user_req, sizeof(add_role_user_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             a_sess, a_csrf, strlen(add_role_user_body), add_role_user_body);

    if (send_http(18081, add_role_user_req, resp, sizeof(resp)) != 0 || strstr(resp, "201") == NULL) {
        printf("FAILED: Failed to create target user for role update test\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    char *rt_pos = strstr(resp, "\"user_id\":");
    long long role_target_id = rt_pos ? atoll(rt_pos + 10) : 0;

    const char *role_change_fm_body = "{\"role\":\"Finance_Manager\"}";
    char admin_role_change_req[512];
    snprintf(admin_role_change_req, sizeof(admin_role_change_req),
             "PATCH /api/organization/users/%lld HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             role_target_id, a_sess, a_csrf, strlen(role_change_fm_body), role_change_fm_body);

    if (send_http(18081, admin_role_change_req, resp, sizeof(resp)) != 0 || strstr(resp, "200 OK") == NULL) {
        printf("FAILED: Administrator role update was not accepted with 200 OK\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Administrator successfully changed user role to Finance_Manager (200 OK)\n");

    char check_role_q[256];
    snprintf(check_role_q, sizeof(check_role_q),
             "SELECT role FROM users WHERE id = %lld;", role_target_id);
    db_client_query(&client, check_role_q);
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    int is_fm = (row && strcmp(row[0], "Finance_Manager") == 0);
    db_client_free_result(res);
    if (!is_fm) {
        printf("FAILED: User role in database was not updated to Finance_Manager\n");
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }

    char fm_login_body[256];
    snprintf(fm_login_body, sizeof(fm_login_body),
             "{\"organization_id\":%lld,\"email\":\"role_target@eptest.com\",\"password\":\"RoleTargetPass#2026\"}",
             org_id);
    char fm_login_req[512];
    snprintf(fm_login_req, sizeof(fm_login_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             strlen(fm_login_body), fm_login_body);

    send_http(18081, fm_login_req, resp, sizeof(resp));
    char fm_sess[128] = {0};
    c_pos = strstr(resp, "session_token=");
    if (c_pos) {
        char *end = strchr(c_pos, ';');
        if (end) {
            size_t len = (size_t)(end - c_pos);
            memcpy(fm_sess, c_pos, len);
            fm_sess[len] = '\0';
        }
    }
    char fm_csrf[128] = {0};
    csrf_pos = strstr(resp, "\"csrf_token\":\"");
    if (csrf_pos) {
        char *start = csrf_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(fm_csrf, start, len);
            fm_csrf[len] = '\0';
        }
    }

    const char *role_change_viewer_body = "{\"role\":\"Viewer\"}";
    char fm_role_change_req[512];
    snprintf(fm_role_change_req, sizeof(fm_role_change_req),
             "PATCH /api/organization/users/%lld HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             role_target_id, fm_sess, fm_csrf, strlen(role_change_viewer_body), role_change_viewer_body);

    if (send_http(18081, fm_role_change_req, resp, sizeof(resp)) != 0 || strstr(resp, "403") == NULL ||
        strstr(resp, "Permission denied: Administrator role required") == NULL) {
        printf("FAILED: Finance Manager role change was not rejected with 403. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Finance Manager cannot change user role (403 Forbidden)\n");

    const char *add_viewer_body = "{\"email\":\"pure_viewer@eptest.com\",\"password\":\"PureViewerPass#2026\",\"role\":\"Viewer\"}";
    char add_viewer_req[512];
    snprintf(add_viewer_req, sizeof(add_viewer_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             a_sess, a_csrf, strlen(add_viewer_body), add_viewer_body);

    send_http(18081, add_viewer_req, resp, sizeof(resp));

    char viewer_login_body[256];
    snprintf(viewer_login_body, sizeof(viewer_login_body),
             "{\"organization_id\":%lld,\"email\":\"pure_viewer@eptest.com\",\"password\":\"PureViewerPass#2026\"}",
             org_id);
    char viewer_login_req[512];
    snprintf(viewer_login_req, sizeof(viewer_login_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             strlen(viewer_login_body), viewer_login_body);

    send_http(18081, viewer_login_req, resp, sizeof(resp));
    char v_sess[128] = {0};
    c_pos = strstr(resp, "session_token=");
    if (c_pos) {
        char *end = strchr(c_pos, ';');
        if (end) {
            size_t len = (size_t)(end - c_pos);
            memcpy(v_sess, c_pos, len);
            v_sess[len] = '\0';
        }
    }
    char v_csrf[128] = {0};
    csrf_pos = strstr(resp, "\"csrf_token\":\"");
    if (csrf_pos) {
        char *start = csrf_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(v_csrf, start, len);
            v_csrf[len] = '\0';
        }
    }

    char viewer_role_change_req[512];
    snprintf(viewer_role_change_req, sizeof(viewer_role_change_req),
             "PATCH /api/organization/users/%lld HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             role_target_id, v_sess, v_csrf, strlen(role_change_viewer_body), role_change_viewer_body);

    if (send_http(18081, viewer_role_change_req, resp, sizeof(resp)) != 0 || strstr(resp, "403") == NULL ||
        strstr(resp, "Permission denied: Administrator role required") == NULL) {
        printf("FAILED: Viewer role change was not rejected with 403. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Viewer cannot change user role (403 Forbidden)\n");

    snprintf(check_role_q, sizeof(check_role_q),
             "SELECT id FROM users WHERE organization_id = %lld AND email = 'admin@eptest.com';", org_id);
    db_client_query(&client, check_role_q);
    res = db_client_store_result(&client);
    row = mysql_fetch_row(res);
    long long admin_a_user_id = row ? atoll(row[0]) : 0;
    db_client_free_result(res);

    char demote_admin_req[512];
    snprintf(demote_admin_req, sizeof(demote_admin_req),
             "PATCH /api/organization/users/%lld HTTP/1.1\r\n"
             "Host: 127.0.0.1:18081\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             admin_a_user_id, a_sess, a_csrf, strlen(role_change_fm_body), role_change_fm_body);

    if (send_http(18081, demote_admin_req, resp, sizeof(resp)) != 0 || strstr(resp, "400") == NULL ||
        strstr(resp, "Cannot demote the last active administrator of the organization") == NULL) {
        printf("FAILED: Demoting final active admin was not rejected with 400. Response:\n%s\n", resp);
        g_run_ep_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Administrator cannot demote the final active Admin (400 Bad Request)\n");

    g_run_ep_server = 0;
    http_server_stop();
#ifdef _WIN32
    WaitForSingleObject((HANDLE)thread_handle, 1000);
    CloseHandle((HANDLE)thread_handle);
#else
    pthread_join(thread_id, NULL);
#endif

    cleanup_test_data(&client);
    db_client_close(&srv_client);
    db_client_close(&client);
    printf("\nALL HTTP ENDPOINT INTEGRATION TESTS SUCCEEDED.\n");
    return 0;
}
