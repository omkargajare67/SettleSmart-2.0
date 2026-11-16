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
#include "../src/http/http_server.h"
#include "../src/websocket/websocket.h"
#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define EXPECT_TRUE(cond, name) do { \
    if (cond) { \
        printf("PASS: %s\n", name); \
        g_tests_passed++; \
    } else { \
        printf("FAIL: %s\n", name); \
        g_tests_failed++; \
    } \
} while(0)

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
        if (strstr(resp_out, "\r\n\r\n")) {
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

static void cleanup_by_org_name(DBClient *client, const char *org_name) {
    char query[512];
    snprintf(query, sizeof(query), "DELETE FROM event_outbox WHERE organization_id IN (SELECT id FROM organizations WHERE name = '%s');", org_name);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "UPDATE transactions SET reversal_of_id = NULL WHERE organization_id IN (SELECT id FROM organizations WHERE name = '%s');", org_name);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name = '%s');", org_name);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name = '%s');", org_name);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM sessions WHERE user_id IN (SELECT id FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name = '%s'));", org_name);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name = '%s');", org_name);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM organizations WHERE name = '%s';", org_name);
    db_client_query(client, query);
}

static void cleanup_test_org(DBClient *client, long long org_id) {
    char query[512];
    snprintf(query, sizeof(query), "DELETE FROM event_outbox WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM sessions WHERE user_id IN (SELECT id FROM users WHERE organization_id = %lld);", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM users WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM organizations WHERE id = %lld;", org_id);
    db_client_query(client, query);
}

int main(void) {
    load_env_file(".env");
    load_env_file("../.env");
    DBConfig db_cfg = {
        .host = get_env_value("DB_HOST", "127.0.0.1"),
        .port = get_env_int("DB_PORT", 3307),
        .database = get_env_value("DB_NAME", "settlesmart_test"),
        .user = get_env_value("DB_USER", "settlesmart_user"),
        .password = get_env_value("DB_PASSWORD", "dev_password_123"),
        .ssl_mode = get_env_value("DB_SSL_MODE", "PREFERRED")
    };

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &db_cfg) != 0) {
        printf("Failed to connect to database\n");
        return 1;
    }

    cleanup_by_org_name(&client, "Org_Ws_Auth_Test");

    long long org_id = 0;
    long long admin_user_id = 0;
    char err_msg[256];
    auth_register_organization(&client, "Org_Ws_Auth_Test", "wsauth@example.com",
                               "AdminPassword123!", &org_id, &admin_user_id, err_msg, sizeof(err_msg));

    char session_token[128];
    char csrf_token[128];
    AuthUserInfo user_info;
    auth_login(&client, org_id, "wsauth@example.com", "AdminPassword123!",
               session_token, sizeof(session_token),
               csrf_token, sizeof(csrf_token),
               &user_info, err_msg, sizeof(err_msg));

    int test_port = 8093;
    HttpServerConfig cfg = {
        .port = test_port,
        .allowed_origins = "http://localhost:3000",
        .app_env = "development",
        .cookie_secure = 0,
        .enable_public_registration = 1,
        .db_client = &client
    };

    EXPECT_TRUE(http_server_start(&cfg) == 0, "HTTP server started for WS auth test");

#ifdef _WIN32
    uintptr_t th = _beginthreadex(NULL, 0, server_thread_func, NULL, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, server_thread_func, NULL);
#endif

#ifdef _WIN32
    Sleep(200);
#else
    usleep(200000);
#endif

    char resp[4096];

    char req_valid[1024];
    snprintf(req_valid, sizeof(req_valid),
             "GET /ws/graph HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: session_token=%s\r\n\r\n", test_port, session_token);
    send_http(test_port, req_valid, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "101 Switching Protocols") != NULL, "Valid handshake returns 101 Switching Protocols");

    char req_nocookie[1024];
    snprintf(req_nocookie, sizeof(req_nocookie),
             "GET /ws/graph HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Origin: http://localhost:3000\r\n\r\n", test_port);
    send_http(test_port, req_nocookie, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "401") != NULL, "Handshake without cookie returns 401");

    char req_badorigin[1024];
    snprintf(req_badorigin, sizeof(req_badorigin),
             "GET /ws/graph HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Origin: http://evil-attacker.com\r\n"
             "Cookie: session_token=%s\r\n\r\n", test_port, session_token);
    send_http(test_port, req_badorigin, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403") != NULL, "Handshake with bad origin returns 403");

    session_revoke(&client, session_token);

    char req_revoked[1024];
    snprintf(req_revoked, sizeof(req_revoked),
             "GET /ws/graph HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: session_token=%s\r\n\r\n", test_port, session_token);
    send_http(test_port, req_revoked, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "401") != NULL, "Handshake with revoked session returns 401");

    g_run_server = 0;
#ifdef _WIN32
    WaitForSingleObject((HANDLE)th, 2000);
    CloseHandle((HANDLE)th);
#else
    pthread_join(th, NULL);
#endif
    http_server_stop();

    cleanup_test_org(&client, org_id);
    db_client_close(&client);

    printf("WebSocket auth tests: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
