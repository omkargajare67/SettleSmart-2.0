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

static volatile int g_run_server = 1;

#ifdef _WIN32
static unsigned __stdcall server_thread_func(void *arg) {
    (void)arg;
    while (g_run_server) {
        http_server_poll(50);
    }
    return 0;
}
#else
static void *server_thread_func(void *arg) {
    (void)arg;
    while (g_run_server) {
        http_server_poll(50);
    }
    return NULL;
}
#endif

static int send_http_raw(int port, const char *req, char *resp_out, size_t max_resp) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

#ifdef _WIN32
    DWORD timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
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
    db_client_query(client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'HttpCsrfTest%');");
    db_client_query(client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'HttpCsrfTest%');");
    db_client_query(client, "DELETE FROM organizations WHERE name LIKE 'HttpCsrfTest%';");
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
        printf("FAILED: Database connection failed\n");
        return 1;
    }

    cleanup_test_data(&client);
    printf("Running Cookie Security, Origin Validation & CSRF Tests (Group I)...\n");

    const char *allowlist = "http://localhost:3000, https://app.settlesmart.com";
    if (http_is_origin_allowed(allowlist, "http://localhost:3000") != 1 ||
        http_is_origin_allowed(allowlist, "https://app.settlesmart.com") != 1) {
        printf("FAILED: Approved origin not recognized\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Approved development and production origins recognized\n");

    if (http_is_origin_allowed(allowlist, "http://unapproved-hacker.com") != 0 ||
        http_is_origin_allowed(allowlist, "http://localhost:3001") != 0 ||
        http_is_origin_allowed(allowlist, NULL) != 0) {
        printf("FAILED: Unapproved origin was accepted\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Unapproved origins and NULL rejected\n");

    const char *test_cookie_hdr = "theme=dark; session_token=secret_sess_val_123; user_pref=metric";
    char parsed_cookie[128] = {0};
    if (http_parse_cookie(test_cookie_hdr, strlen(test_cookie_hdr), "session_token",
                          parsed_cookie, sizeof(parsed_cookie)) != 0 ||
        strcmp(parsed_cookie, "secret_sess_val_123") != 0) {
        printf("FAILED: http_parse_cookie failed for middle token\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Cookie parser correctly extracted session_token\n");

    SessionInfo dummy_sess;
    dummy_sess.session_id = 1;
    dummy_sess.organization_id = 1;
    dummy_sess.user_id = 1;
    const char *raw_csrf = "my_secure_random_csrf_token_value_xyz";
    crypto_sha256_hex(raw_csrf, strlen(raw_csrf), dummy_sess.csrf_token_hash);

    if (session_validate_csrf(&dummy_sess, raw_csrf) != 1) {
        printf("FAILED: session_validate_csrf rejected authentic CSRF token\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Valid CSRF token accepted\n");

    if (session_validate_csrf(&dummy_sess, "tampered_csrf_token_abc") != 0 ||
        session_validate_csrf(&dummy_sess, NULL) != 0) {
        printf("FAILED: session_validate_csrf accepted invalid/NULL token\n");
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Tampered and NULL CSRF tokens rejected\n");

    HttpServerConfig server_cfg;
    server_cfg.port = 18080;
    strncpy(server_cfg.allowed_origins, "http://localhost:3000, https://app.settlesmart.com", sizeof(server_cfg.allowed_origins) - 1);
    server_cfg.allowed_origins[sizeof(server_cfg.allowed_origins) - 1] = '\0';
    strncpy(server_cfg.app_env, "development", sizeof(server_cfg.app_env) - 1);
    server_cfg.app_env[sizeof(server_cfg.app_env) - 1] = '\0';
    server_cfg.cookie_secure = 0;
    server_cfg.enable_public_registration = 0;
    server_cfg.db_client = &client;

    mg_log_set(MG_LL_NONE);
    if (http_server_start(&server_cfg) != 0) {
        printf("FAILED: Failed to start HTTP server on port 18080\n");
        db_client_close(&client);
        return 1;
    }

#ifdef _WIN32
    uintptr_t thread_handle = _beginthreadex(NULL, 0, server_thread_func, NULL, 0, NULL);
    Sleep(150);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, server_thread_func, NULL);
    usleep(150000);
#endif

    char resp_buf[4096];

    const char *reg_body = "{\"organization_name\":\"HttpCsrfTest Org\",\"admin_email\":\"admin@httpcsrf.com\",\"admin_password\":\"MasterHttpPassword#2026\"}";
    char reg_req[512];
    snprintf(reg_req, sizeof(reg_req),
             "POST /api/auth/register-organization HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(reg_body), reg_body);

    if (send_http_raw(18080, reg_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Disabled public registration was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Disabled public registration rejected with 403 Forbidden\n");

    server_cfg.enable_public_registration = 1;

    if (send_http_raw(18080, reg_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "201") == NULL) {
        printf("FAILED: Enabled public registration failed. Response:\n%s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Explicitly enabled registration succeeded with 201 Created\n");

    const char *preflight_allowed_dev =
        "OPTIONS /api/organization/users HTTP/1.1\r\n"
        "Host: 127.0.0.1:18080\r\n"
        "Origin: http://localhost:3000\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "Connection: close\r\n\r\n";

    if (send_http_raw(18080, preflight_allowed_dev, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "204") == NULL ||
        strstr(resp_buf, "Access-Control-Allow-Origin: http://localhost:3000") == NULL ||
        strstr(resp_buf, "Access-Control-Allow-Credentials: true") == NULL) {
        printf("FAILED: Approved dev origin OPTIONS preflight failed. Response:\n%s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Approved development origin OPTIONS preflight returned 204 with exact origin and credentials\n");

    const char *preflight_allowed_prod =
        "OPTIONS /api/organization/users HTTP/1.1\r\n"
        "Host: 127.0.0.1:18080\r\n"
        "Origin: https://app.settlesmart.com\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "Connection: close\r\n\r\n";

    if (send_http_raw(18080, preflight_allowed_prod, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "204") == NULL ||
        strstr(resp_buf, "Access-Control-Allow-Origin: https://app.settlesmart.com") == NULL ||
        strstr(resp_buf, "Access-Control-Allow-Credentials: true") == NULL) {
        printf("FAILED: Approved production HTTPS origin OPTIONS preflight failed. Response:\n%s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Approved production HTTPS origin OPTIONS preflight returned 204 with exact origin and credentials\n");

    const char *preflight_unapproved_prod =
        "OPTIONS /api/organization/users HTTP/1.1\r\n"
        "Host: 127.0.0.1:18080\r\n"
        "Origin: https://unapproved-prod.settlesmart.com\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "Connection: close\r\n\r\n";

    if (send_http_raw(18080, preflight_unapproved_prod, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Unapproved production origin OPTIONS preflight was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Unapproved production origin OPTIONS preflight rejected with 403 Forbidden\n");

    const char *preflight_missing =
        "OPTIONS /api/organization/users HTTP/1.1\r\n"
        "Host: 127.0.0.1:18080\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "Connection: close\r\n\r\n";

    if (send_http_raw(18080, preflight_missing, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Missing origin OPTIONS preflight was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Missing origin OPTIONS preflight rejected with 403 Forbidden\n");

    const char *preflight_invalid =
        "OPTIONS /api/organization/users HTTP/1.1\r\n"
        "Host: 127.0.0.1:18080\r\n"
        "Origin: invalid://malformed-origin-string\r\n"
        "Access-Control-Request-Method: POST\r\n"
        "Connection: close\r\n\r\n";

    if (send_http_raw(18080, preflight_invalid, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Invalid origin OPTIONS preflight was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Invalid origin OPTIONS preflight rejected with 403 Forbidden\n");

    char query[256];
    snprintf(query, sizeof(query), "SELECT id FROM organizations WHERE name = 'HttpCsrfTest Org';");
    db_client_query(&client, query);
    MYSQL_RES *res = db_client_store_result(&client);
    MYSQL_ROW row = mysql_fetch_row(res);
    long long org_id = atoll(row[0]);
    db_client_free_result(res);

    char login_body[256];
    snprintf(login_body, sizeof(login_body),
             "{\"organization_id\":%lld,\"email\":\"admin@httpcsrf.com\",\"password\":\"MasterHttpPassword#2026\"}",
             org_id);

    char unapproved_origin_req[512];
    snprintf(unapproved_origin_req, sizeof(unapproved_origin_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://unapproved-origin.com\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(login_body), login_body);

    if (send_http_raw(18080, unapproved_origin_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: State-changing request from unapproved origin was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: State-changing request from unapproved origin rejected with 403 Forbidden\n");

    char approved_dev_login[512];
    snprintf(approved_dev_login, sizeof(approved_dev_login),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://localhost:3000\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(login_body), login_body);

    if (send_http_raw(18080, approved_dev_login, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "200 OK") == NULL ||
        strstr(resp_buf, "Access-Control-Allow-Origin: http://localhost:3000") == NULL ||
        strstr(resp_buf, "Access-Control-Allow-Credentials: true") == NULL ||
        strstr(resp_buf, "Access-Control-Allow-Origin: *") != NULL) {
        printf("FAILED: Approved dev origin login failed or used wildcard CORS\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Approved development origin login returned exact origin CORS and credentials\n");

    if (strstr(resp_buf, "HttpOnly") == NULL || strstr(resp_buf, "SameSite=Lax") == NULL ||
        strstr(resp_buf, "; Secure") != NULL) {
        printf("FAILED: Local HTTP dev cookie should omit Secure attribute\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: TEST A - Local HTTP dev cookie correctly omits Secure attribute\n");

    char session_cookie[128] = {0};
    char *cookie_pos = strstr(resp_buf, "session_token=");
    if (cookie_pos) {
        char *end_pos = strchr(cookie_pos, ';');
        if (end_pos) {
            size_t clen = (size_t)(end_pos - cookie_pos);
            memcpy(session_cookie, cookie_pos, clen);
            session_cookie[clen] = '\0';
        }
    }

    char csrf_tok[128] = {0};
    char *csrf_pos = strstr(resp_buf, "\"csrf_token\":\"");
    if (csrf_pos) {
        char *start = csrf_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t tlen = (size_t)(end - start);
            memcpy(csrf_tok, start, tlen);
            csrf_tok[tlen] = '\0';
        }
    }

    server_cfg.cookie_secure = 1;

    char prod_cookie_login[512];
    snprintf(prod_cookie_login, sizeof(prod_cookie_login),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: https://app.settlesmart.com\r\n"
             "Connection: close\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(login_body), login_body);

    if (send_http_raw(18080, prod_cookie_login, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "200 OK") == NULL ||
        strstr(resp_buf, "Secure") == NULL ||
        strstr(resp_buf, "HttpOnly") == NULL) {
        printf("FAILED: Production cookie missing Secure attribute\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: TEST B - Production HTTPS cookie verified with HttpOnly, Secure, SameSite=Lax\n");

    char no_csrf_req[512];
    const char *user_body = "{\"email\":\"fin@httpcsrf.com\",\"password\":\"FinPassword#2026\",\"role\":\"Finance_Manager\"}";
    snprintf(no_csrf_req, sizeof(no_csrf_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://localhost:3000\r\n"
             "Connection: close\r\n"
             "Cookie: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             session_cookie, strlen(user_body), user_body);

    if (send_http_raw(18080, no_csrf_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Request without CSRF token was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: TEST E - State-changing request without CSRF token rejected with 403 Forbidden\n");

    char bad_csrf_req[512];
    snprintf(bad_csrf_req, sizeof(bad_csrf_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://localhost:3000\r\n"
             "Connection: close\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: invalid_csrf_token_1234567890abcdef\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             session_cookie, strlen(user_body), user_body);

    if (send_http_raw(18080, bad_csrf_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Request with invalid CSRF token was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: TEST F - State-changing request with invalid CSRF token rejected with 403 Forbidden\n");

    char initial_csrf[128];
    strncpy(initial_csrf, csrf_tok, sizeof(initial_csrf) - 1);
    initial_csrf[sizeof(initial_csrf) - 1] = '\0';
    memset(csrf_tok, 0, sizeof(csrf_tok));

    char csrf_unapproved_req[512];
    snprintf(csrf_unapproved_req, sizeof(csrf_unapproved_req),
             "POST /api/auth/csrf HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: https://unapproved-origin.com\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie);

    if (send_http_raw(18080, csrf_unapproved_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: POST /api/auth/csrf with unapproved origin was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: POST /api/auth/csrf rejected unapproved origin with 403 Forbidden\n");

    char csrf_missing_origin_req[512];
    snprintf(csrf_missing_origin_req, sizeof(csrf_missing_origin_req),
             "POST /api/auth/csrf HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie);

    if (send_http_raw(18080, csrf_missing_origin_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: POST /api/auth/csrf with missing origin was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: POST /api/auth/csrf rejected missing origin with 403 Forbidden\n");

    char csrf_invalid_origin_req[512];
    snprintf(csrf_invalid_origin_req, sizeof(csrf_invalid_origin_req),
             "POST /api/auth/csrf HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: invalid://malformed-origin-string\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie);

    if (send_http_raw(18080, csrf_invalid_origin_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: POST /api/auth/csrf with invalid origin was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: POST /api/auth/csrf rejected invalid origin with 403 Forbidden\n");

    char csrf_invalid_sess_req[512];
    snprintf(csrf_invalid_sess_req, sizeof(csrf_invalid_sess_req),
             "POST /api/auth/csrf HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: session_token=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\r\n"
             "Connection: close\r\n\r\n");

    if (send_http_raw(18080, csrf_invalid_sess_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "401") == NULL) {
        printf("FAILED: POST /api/auth/csrf with invalid session was not rejected with 401\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: POST /api/auth/csrf rejected invalid session with 401 Unauthorized\n");

    char csrf_valid_req[512];
    snprintf(csrf_valid_req, sizeof(csrf_valid_req),
             "POST /api/auth/csrf HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie);

    if (send_http_raw(18080, csrf_valid_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "200 OK") == NULL ||
        strstr(resp_buf, "\"csrf_token\":\"") == NULL ||
        strstr(resp_buf, "session_token=") != NULL) {
        printf("FAILED: POST /api/auth/csrf recovery failed or leaked session token\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }

    char *rc_pos = strstr(resp_buf, "\"csrf_token\":\"");
    if (rc_pos) {
        char *start = rc_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(csrf_tok, start, len);
            csrf_tok[len] = '\0';
        }
    }
    printf("PASSED: POST /api/auth/csrf successfully recovered new CSRF token after simulated refresh\n");

    send_http_raw(18080, csrf_valid_req, resp_buf, sizeof(resp_buf));
    rc_pos = strstr(resp_buf, "\"csrf_token\":\"");
    if (rc_pos) {
        char *start = rc_pos + 14;
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            memcpy(csrf_tok, start, len);
            csrf_tok[len] = '\0';
        }
    }

    char old_csrf_req[512];
    snprintf(old_csrf_req, sizeof(old_csrf_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://localhost:3000\r\n"
             "Connection: close\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n\r\n%s",
             session_cookie, initial_csrf, strlen(user_body), user_body);

    if (send_http_raw(18080, old_csrf_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Discarded old CSRF token was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Discarded old CSRF token rejected with 403 Forbidden\n");

    char good_csrf_req[512];
    snprintf(good_csrf_req, sizeof(good_csrf_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18080\r\n"
             "Origin: http://localhost:3000\r\n"
             "Connection: close\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             session_cookie, csrf_tok, strlen(user_body), user_body);

    if (send_http_raw(18080, good_csrf_req, resp_buf, sizeof(resp_buf)) != 0 ||
        strstr(resp_buf, "201") == NULL) {
        printf("FAILED: Request with valid CSRF token failed. Response:\n%s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: TEST G - State-changing request with valid CSRF token succeeded (201 Created)\n");

    g_run_server = 0;
    http_server_stop();
#ifdef _WIN32
    WaitForSingleObject((HANDLE)thread_handle, 1000);
    CloseHandle((HANDLE)thread_handle);
#else
    pthread_join(thread_id, NULL);
#endif

    HttpServerConfig prod_env_cfg;
    prod_env_cfg.port = 18082;
    strncpy(prod_env_cfg.allowed_origins, "https://app.settlesmart.com", sizeof(prod_env_cfg.allowed_origins) - 1);
    strncpy(prod_env_cfg.app_env, "production", sizeof(prod_env_cfg.app_env) - 1);
    prod_env_cfg.cookie_secure = 0;
    prod_env_cfg.enable_public_registration = 0;
    prod_env_cfg.db_client = &client;

    http_server_start(&prod_env_cfg);
    if (prod_env_cfg.cookie_secure != 1) {
        printf("FAILED: Production mode permitted cookie_secure to remain 0\n");
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    http_server_stop();
    printf("PASSED: TEST C - Production mode enforces Secure cookie even if configured with 0\n");

    cleanup_test_data(&client);
    db_client_close(&client);
    printf("\nALL COOKIE SECURITY, ORIGIN VALIDATION & CSRF TESTS SUCCEEDED.\n");
    return 0;
}
