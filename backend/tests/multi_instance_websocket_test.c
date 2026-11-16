#include "../src/config/env_loader.h"
#include "../src/database/db_client.h"
#include "../src/http/http_server.h"
#include "../src/websocket/outbox_consumer.h"
#include "../src/websocket/event_dispatcher.h"
#include "../src/transactions/transaction_service.h"
#include "../src/entities/entity_service.h"
#include "../src/auth/auth_service.h"
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

    db_client_query(&client_a, "DELETE FROM outbox_consumer_cursors;");
    db_client_query(&client_a, "DELETE FROM event_outbox;");
    db_client_query(&client_a, "DELETE FROM transactions WHERE idempotency_key LIKE '%multiws%';");
    db_client_query(&client_a, "DELETE FROM entities WHERE name LIKE '%MultiWs%';");
    db_client_query(&client_a, "DELETE FROM sessions;");
    db_client_query(&client_a, "DELETE FROM users WHERE email LIKE '%multiws%';");
    db_client_query(&client_a, "DELETE FROM organizations WHERE name = 'MultiWs Org';");

    long long org_id = 0;
    long long user_id = 0;
    char err_buf[256];

    if (auth_register_organization(&client_a, "MultiWs Org", "admin@multiws.com",
                                   "AdminSecret#2026", &org_id, &user_id,
                                   err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Organization registration failed: %s\n", err_buf);
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }

    Entity e1, e2;
    entity_service_create(&client_a, org_id, "MultiWs Ent 1", "Department", user_id, &e1, err_buf, sizeof(err_buf));
    entity_service_create(&client_a, org_id, "MultiWs Ent 2", "Vendor", user_id, &e2, err_buf, sizeof(err_buf));

    char sess_token[128];
    char csrf_token[128];
    UserInfo uinfo;
    auth_login(&client_a, org_id, "admin@multiws.com", "AdminSecret#2026",
               sess_token, sizeof(sess_token), csrf_token, sizeof(csrf_token),
               &uinfo, err_buf, sizeof(err_buf));

    int srv_b_port = 18095;
    HttpServerConfig srv_b_cfg;
    memset(&srv_b_cfg, 0, sizeof(srv_b_cfg));
    srv_b_cfg.port = srv_b_port;
    strncpy(srv_b_cfg.allowed_origins, "http://localhost:3000", sizeof(srv_b_cfg.allowed_origins) - 1);
    strncpy(srv_b_cfg.app_env, "development", sizeof(srv_b_cfg.app_env) - 1);
    srv_b_cfg.enable_public_registration = 1;
    srv_b_cfg.db_client = &client_b;
    strncpy(srv_b_cfg.instance_id, "instance_B", sizeof(srv_b_cfg.instance_id) - 1);

    if (http_server_start(&srv_b_cfg) != 0) {
        printf("FAILED: http_server_start on port %d failed\n", srv_b_port);
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }

#ifdef _WIN32
    HANDLE hThreadB = (HANDLE)_beginthreadex(NULL, 0, server_thread_func, NULL, 0, NULL);
    Sleep(200);
#else
    pthread_t threadB;
    pthread_create(&threadB, NULL, server_thread_func, NULL);
    usleep(200000);
#endif

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int ws_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ws_sock < 0) {
        printf("FAILED: Failed to create socket for Instance B client\n");
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }

#ifdef _WIN32
    DWORD timeout = 4000;
    setsockopt(ws_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 4;
    tv.tv_usec = 0;
    setsockopt(ws_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)srv_b_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(ws_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("FAILED: Failed to connect to Instance B on port %d\n", srv_b_port);
#ifdef _WIN32
        closesocket(ws_sock);
#else
        close(ws_sock);
#endif
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }

    char ws_req[1024];
    snprintf(ws_req, sizeof(ws_req),
             "GET /ws/graph HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n"
             "Origin: http://localhost:3000\r\n"
             "Cookie: session_token=%s\r\n\r\n",
             srv_b_port, sess_token);

    send(ws_sock, ws_req, (int)strlen(ws_req), 0);

    char hs_resp[2048];
    int hs_len = recv(ws_sock, hs_resp, sizeof(hs_resp) - 1, 0);
    if (hs_len <= 0 || strstr(hs_resp, "101") == NULL) {
        printf("FAILED: WebSocket handshake on Instance B failed: %s\n", hs_resp);
#ifdef _WIN32
        closesocket(ws_sock);
#else
        close(ws_sock);
#endif
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: WebSocket client successfully connected to Instance B (port %d)\n", srv_b_port);

    Transaction tx1;
    if (transaction_service_create(&client_a, org_id, e1.id, e2.id, 250000, "INR",
                                   "multiws_key_1", "fp_multiws", user_id, &tx1,
                                   err_buf, sizeof(err_buf)) != 0) {
        printf("FAILED: Transaction creation on Instance A failed: %s\n", err_buf);
#ifdef _WIN32
        closesocket(ws_sock);
#else
        close(ws_sock);
#endif
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: Transaction created via Instance A (Tx ID: %lld)\n", tx1.id);

#ifdef _WIN32
    Sleep(300);
#else
    usleep(300000);
#endif

    char ws_frame[4096];
    int frame_len = recv(ws_sock, ws_frame, sizeof(ws_frame) - 1, 0);
    if (frame_len <= 0) {
        printf("FAILED: Client on Instance B received no event frame from Instance A\n");
#ifdef _WIN32
        closesocket(ws_sock);
#else
        close(ws_sock);
#endif
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    ws_frame[frame_len] = '\0';

    if (strstr(ws_frame, "TRANSACTION_CREATED") == NULL) {
        printf("FAILED: Received WS frame does not contain TRANSACTION_CREATED: %s\n", ws_frame);
#ifdef _WIN32
        closesocket(ws_sock);
#else
        close(ws_sock);
#endif
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: Client on Instance B received TRANSACTION_CREATED event generated by Instance A\n");

    int64_t cur_b = 0;
    outbox_consumer_get_cursor(&client_b, "instance_B", org_id, &cur_b);
    if (cur_b < 1) {
        printf("FAILED: Instance B consumer cursor was not advanced: %lld\n", (long long)cur_b);
        g_run_server = 0;
        http_server_stop();
        db_client_close(&client_a);
        db_client_close(&client_b);
        return 1;
    }
    printf("PASSED: Instance B consumer cursor advanced in database to sequence %lld\n", (long long)cur_b);

#ifdef _WIN32
    closesocket(ws_sock);
#else
    close(ws_sock);
#endif

    g_run_server = 0;
    http_server_stop();

#ifdef _WIN32
    WaitForSingleObject(hThreadB, 1000);
    CloseHandle(hThreadB);
#else
    pthread_join(threadB, NULL);
#endif

    db_client_close(&client_a);
    db_client_close(&client_b);
    printf("\nALL MULTI-INSTANCE WEBSOCKET TESTS SUCCEEDED.\n");
    return 0;
}

