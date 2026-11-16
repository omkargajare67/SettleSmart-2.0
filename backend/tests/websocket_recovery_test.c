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
#include "../src/entities/entity_service.h"
#include "../src/transactions/transaction_service.h"
#include "../src/websocket/websocket.h"
#include "../src/websocket/event_dispatcher.h"
#include "../src/http/http_server.h"
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
        http_server_poll(20);
    }
    return 0;
}
#else
static void *server_thread_func(void *arg) {
    (void)arg;
    while (g_run_server) {
        http_server_poll(20);
    }
    return NULL;
}
#endif

static void send_ws_text(int sock, const char *text) {
    size_t len = strlen(text);
    unsigned char frame[1024];
    frame[0] = 0x81;
    frame[1] = (unsigned char)(0x80 | len);
    frame[2] = 0x00;
    frame[3] = 0x00;
    frame[4] = 0x00;
    frame[5] = 0x00;
    memcpy(frame + 6, text, len);
    send(sock, (const char *)frame, (int)(len + 6), 0);
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
    snprintf(query, sizeof(query), "UPDATE transactions SET reversal_of_id = NULL WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM transactions WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM entities WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM sessions WHERE user_id IN (SELECT id FROM users WHERE organization_id = %lld);", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM users WHERE organization_id = %lld;", org_id);
    db_client_query(client, query);
    snprintf(query, sizeof(query), "DELETE FROM organizations WHERE id = %lld;", org_id);
    db_client_query(client, query);
}

static int connect_ws_client(int port, const char *session_token) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

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

    char req[1024];
    snprintf(req, sizeof(req),
             "GET /ws/graph HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: session_token=%s\r\n\r\n", port, session_token);
    send(sock, req, (int)strlen(req), 0);

    char resp[2048];
    int r = recv(sock, resp, sizeof(resp) - 1, 0);
    if (r <= 0 || strstr(resp, "101") == NULL) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    return sock;
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

    cleanup_by_org_name(&client, "Org_Ws_Recov_Test");

    long long org_id = 0, admin_id = 0;
    char err_msg[256];
    auth_register_organization(&client, "Org_Ws_Recov_Test", "wsrecov@example.com",
                               "AdminPassword123!", &org_id, &admin_id, err_msg, sizeof(err_msg));

    char session_token[128], csrf_token[128];
    AuthUserInfo u;
    auth_login(&client, org_id, "wsrecov@example.com", "AdminPassword123!",
               session_token, sizeof(session_token), csrf_token, sizeof(csrf_token), &u, err_msg, sizeof(err_msg));

    Entity e1, e2;
    entity_service_create(&client, org_id, admin_id, "E_Recov_1", ENTITY_TYPE_DEPARTMENT, &e1, err_msg, sizeof(err_msg));
    entity_service_create(&client, org_id, admin_id, "E_Recov_2", ENTITY_TYPE_VENDOR, &e2, err_msg, sizeof(err_msg));

    int test_port = 8096;
    HttpServerConfig cfg = {
        .port = test_port,
        .allowed_origins = "http://localhost:3000",
        .app_env = "development",
        .cookie_secure = 0,
        .enable_public_registration = 1,
        .db_client = &client
    };

    EXPECT_TRUE(http_server_start(&cfg) == 0, "HTTP server started for WS recovery test");

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

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock1 = connect_ws_client(test_port, session_token);
    EXPECT_TRUE(sock1 >= 0, "Initial WS client connected");

    Transaction t1;
    int is_replay = 0;
    transaction_service_create(&client, org_id, admin_id, e1.id, e2.id,
                               "100.00", "INR", "d0000000-0000-4000-8000-000000000001",
                               &t1, &is_replay, err_msg, sizeof(err_msg));

    char buf[4096];
    int len = recv(sock1, buf, sizeof(buf) - 1, 0);
    if (len > 0) buf[len] = '\0'; else buf[0] = '\0';
    EXPECT_TRUE(strstr(buf, "TRANSACTION_CREATED") != NULL && strstr(buf, "100.00") != NULL, "Client received first event");

#ifdef _WIN32
    closesocket(sock1);
#else
    close(sock1);
#endif

#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif

    Transaction t2, t3;
    transaction_service_create(&client, org_id, admin_id, e1.id, e2.id,
                               "200.00", "INR", "d0000000-0000-4000-8000-000000000002",
                               &t2, &is_replay, err_msg, sizeof(err_msg));
    transaction_service_create(&client, org_id, admin_id, e2.id, e1.id,
                               "300.00", "INR", "d0000000-0000-4000-8000-000000000003",
                               &t3, &is_replay, err_msg, sizeof(err_msg));

    int64_t watermark = 0;
    outbox_get_watermark(&client, org_id, &watermark);
    EXPECT_TRUE(watermark == 3, "Outbox watermark matches total events (3)");

    int sock2 = connect_ws_client(test_port, session_token);
    EXPECT_TRUE(sock2 >= 0, "Reconnected WS client");

#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif

    send_ws_text(sock2, "{\"type\":\"SUBSCRIBE_GRAPH\",\"last_seen_sequence\":1}");

#ifdef _WIN32
    Sleep(200);
#else
    usleep(200000);
#endif

    char replay_buf[8192];
    int rlen = recv(sock2, replay_buf, sizeof(replay_buf) - 1, 0);
    if (rlen > 0) replay_buf[rlen] = '\0'; else replay_buf[0] = '\0';

    EXPECT_TRUE(strstr(replay_buf, "200.00") != NULL, "Replayed missed event 2 (200.00)");
    EXPECT_TRUE(strstr(replay_buf, "300.00") != NULL, "Replayed missed event 3 (300.00)");

#ifdef _WIN32
    closesocket(sock2);
#else
    close(sock2);
#endif

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

    printf("WebSocket recovery tests: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
