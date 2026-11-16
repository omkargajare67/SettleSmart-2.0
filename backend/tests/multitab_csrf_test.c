#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/http/http_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#endif

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
    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)port);
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

    size_t total_recv = 0;
    int bytes = 0;
    while ((bytes = recv(sock, resp_out + total_recv, (int)(max_resp - 1 - total_recv), 0)) > 0) {
        total_recv += bytes;
        if (total_recv >= max_resp - 1) {
            break;
        }
    }
    resp_out[total_recv] = '\0';

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return 0;
}

static void parse_json_value(const char *json, const char *key, char *out, size_t max_out) {
    out[0] = '\0';
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":\"", key);
    char *pos = strstr(json, search_pattern);
    if (!pos) {
        return;
    }
    char *start = pos + strlen(search_pattern);
    char *end = strchr(start, '\"');
    if (!end) {
        return;
    }
    size_t len = (size_t)(end - start);
    if (len >= max_out) {
        len = max_out - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
}

int main(void) {
    load_env_file(".env");
    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &db_cfg) != 0) {
        printf("FAILED: Database connection failed\n");
        return 1;
    }

    db_client_query(&client, "DELETE FROM sessions;");
    db_client_query(&client, "DELETE FROM users WHERE email LIKE '%multitab%';");
    db_client_query(&client, "DELETE FROM organizations WHERE name = 'MultiTab Org';");

    HttpServerConfig srv_cfg;
    memset(&srv_cfg, 0, sizeof(srv_cfg));
    srv_cfg.port = 18091;
    strncpy(srv_cfg.allowed_origins, "http://localhost:3000", sizeof(srv_cfg.allowed_origins) - 1);
    strncpy(srv_cfg.app_env, "development", sizeof(srv_cfg.app_env) - 1);
    srv_cfg.enable_public_registration = 1;
    srv_cfg.db_client = &client;
    strncpy(srv_cfg.instance_id, "inst_multitab", sizeof(srv_cfg.instance_id) - 1);

    if (http_server_start(&srv_cfg) != 0) {
        printf("FAILED: http_server_start failed on port 18091\n");
        db_client_close(&client);
        return 1;
    }

#ifdef _WIN32
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, server_thread_func, NULL, 0, NULL);
    Sleep(200);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, server_thread_func, NULL);
    usleep(200000);
#endif

    char resp_buf[8192];

    const char *reg_body = "{\"organization_name\":\"MultiTab Org\",\"admin_email\":\"admin@multitab.com\",\"admin_password\":\"StrongAdminPass#2026\"}";
    char reg_req[512];
    snprintf(reg_req, sizeof(reg_req),
        "POST /api/auth/register-organization HTTP/1.1\r\n"
        "Host: 127.0.0.1:18091\r\n"
        "Origin: http://localhost:3000\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s",
        strlen(reg_body), reg_body);

    if (send_http_raw(18091, reg_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "201 Created") == NULL) {
        printf("FAILED: Registration failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Multi-tab test organization registered\n");

    long long oid = 0;
    char *oid_pos = strstr(resp_buf, "\"organization_id\":");
    if (oid_pos) {
        oid = atoll(oid_pos + 18);
    }

    char login_body[256];
    snprintf(login_body, sizeof(login_body),
             "{\"organization_id\":%lld,\"email\":\"admin@multitab.com\",\"password\":\"StrongAdminPass#2026\"}",
             oid);
    char login_req[512];
    snprintf(login_req, sizeof(login_req),
             "POST /api/auth/login HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://localhost:3000\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             strlen(login_body), login_body);

    if (send_http_raw(18091, login_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "200 OK") == NULL) {
        printf("FAILED: Login failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }

    char session_cookie[256];
    char csrf_tok1[128];
    char *c_start = strstr(resp_buf, "Set-Cookie: ");
    if (c_start) {
        c_start += 12;
        char *c_end = strchr(c_start, ';');
        if (c_end) {
            size_t clen = (size_t)(c_end - c_start);
            memcpy(session_cookie, c_start, clen);
            session_cookie[clen] = '\0';
        }
    }
    parse_json_value(resp_buf, "csrf_token", csrf_tok1, sizeof(csrf_tok1));
    printf("PASSED: Initial login issued Session Cookie and CSRF Token 1\n");

    char refresh_req[512];
    snprintf(refresh_req, sizeof(refresh_req),
             "POST /api/auth/csrf HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: %s\r\n"
             "Connection: close\r\n\r\n",
             session_cookie);

    if (send_http_raw(18091, refresh_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "200 OK") == NULL) {
        printf("FAILED: Tab 2 CSRF token request failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }

    char csrf_tok2[128];
    parse_json_value(resp_buf, "csrf_token", csrf_tok2, sizeof(csrf_tok2));
    printf("PASSED: Tab 2 retrieved fresh CSRF Token 2\n");

    const char *u1_body = "{\"email\":\"manager1@multitab.com\",\"password\":\"ManagerPass#2026\",\"role\":\"Finance_Manager\"}";
    char tab1_req[1024];
    snprintf(tab1_req, sizeof(tab1_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             session_cookie, csrf_tok1, strlen(u1_body), u1_body);

    if (send_http_raw(18091, tab1_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "201 Created") == NULL) {
        printf("FAILED: Tab 1 request with Token 1 failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Tab 1 state-changing request with previous CSRF token succeeded (dual-slot tolerance)\n");

    const char *u2_body = "{\"email\":\"manager2@multitab.com\",\"password\":\"ManagerPass#2026\",\"role\":\"Finance_Manager\"}";
    char tab2_req[1024];
    snprintf(tab2_req, sizeof(tab2_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             session_cookie, csrf_tok2, strlen(u2_body), u2_body);

    if (send_http_raw(18091, tab2_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "201 Created") == NULL) {
        printf("FAILED: Tab 2 request with Token 2 failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Tab 2 state-changing request with current CSRF token succeeded\n");

    if (send_http_raw(18091, refresh_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "200 OK") == NULL) {
        printf("FAILED: Tab 3 CSRF token request failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }

    char csrf_tok3[128];
    parse_json_value(resp_buf, "csrf_token", csrf_tok3, sizeof(csrf_tok3));
    printf("PASSED: Tab 3 retrieved fresh CSRF Token 3\n");

    const char *u3_body = "{\"email\":\"viewer1@multitab.com\",\"password\":\"ViewerPass#2026\",\"role\":\"Viewer\"}";
    char tab1_evicted_req[1024];
    snprintf(tab1_evicted_req, sizeof(tab1_evicted_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             session_cookie, csrf_tok1, strlen(u3_body), u3_body);

    if (send_http_raw(18091, tab1_evicted_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Evicted Token 1 was not rejected with 403: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Evicted Token 1 correctly rejected with 403 Forbidden\n");

    char tab2_again_req[1024];
    snprintf(tab2_again_req, sizeof(tab2_again_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             session_cookie, csrf_tok2, strlen(u3_body), u3_body);

    if (send_http_raw(18091, tab2_again_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "201 Created") == NULL) {
        printf("FAILED: Token 2 (now in previous slot) failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Token 2 succeeded in previous slot\n");

    char tab3_req[1024];
    const char *u4_body = "{\"email\":\"viewer2@multitab.com\",\"password\":\"ViewerPass#2026\",\"role\":\"Viewer\"}";
    snprintf(tab3_req, sizeof(tab3_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             session_cookie, csrf_tok3, strlen(u4_body), u4_body);

    if (send_http_raw(18091, tab3_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "201 Created") == NULL) {
        printf("FAILED: Token 3 request failed: %s\n", resp_buf);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Token 3 succeeded in current slot\n");

    char bad_origin_req[1024];
    snprintf(bad_origin_req, sizeof(bad_origin_req),
             "POST /api/organization/users HTTP/1.1\r\n"
             "Host: 127.0.0.1:18091\r\n"
             "Origin: http://malicious-attacker.com\r\n"
             "Cookie: %s\r\n"
             "X-CSRF-Token: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             session_cookie, csrf_tok3, strlen(u4_body), u4_body);

    if (send_http_raw(18091, bad_origin_req, resp_buf, sizeof(resp_buf)) != 0 || strstr(resp_buf, "403") == NULL) {
        printf("FAILED: Malicious origin was not rejected with 403\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client);
        return 1;
    }
    printf("PASSED: Malicious origin rejected with 403 Forbidden\n");

    g_run_server = 0;
    http_server_stop();

#ifdef _WIN32
    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);
#else
    pthread_join(thread_id, NULL);
#endif

    db_client_close(&client);
    printf("\nALL MULTI-TAB CSRF AND DUAL-SLOT TESTS SUCCEEDED.\n");
    return 0;
}
