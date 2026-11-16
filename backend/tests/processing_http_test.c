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
#include "../src/entities/entity_service.h"
#include "../src/transactions/transaction_service.h"
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

static int send_request(int port, const char *method, const char *path,
                        const char *cookie, const char *csrf,
                        const char *body, char *resp, size_t max_resp) {
    char req[4096];
    size_t body_len = body ? strlen(body) : 0;
    char headers[1024] = {0};

    if (cookie && strlen(cookie) > 0) {
        snprintf(headers + strlen(headers), sizeof(headers) - strlen(headers),
                 "Cookie: %s\r\n", cookie);
    }
    if (csrf && strlen(csrf) > 0) {
        snprintf(headers + strlen(headers), sizeof(headers) - strlen(headers),
                 "X-CSRF-Token: %s\r\n", csrf);
    }
    if (body_len > 0) {
        snprintf(headers + strlen(headers), sizeof(headers) - strlen(headers),
                 "Content-Type: application/json\r\nContent-Length: %zu\r\n", body_len);
    }

    snprintf(req, sizeof(req),
             "%s %s HTTP/1.1\r\n"
             "Host: 127.0.0.1:%d\r\n"
             "Origin: http://localhost:3000\r\n"
             "Connection: close\r\n"
             "%s"
             "\r\n"
             "%s",
             method, path, port, headers, body ? body : "");

    return send_http(port, req, resp, max_resp);
}

static void cleanup_org(DBClient *client, const char *org_name) {
    char query[512];
    snprintf(query, sizeof(query), "SELECT id FROM organizations WHERE name = '%s';", org_name);
    if (db_client_query(client, query) == 0) {
        MYSQL_RES *res = db_client_store_result(client);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                long long org_id = atoll(row[0]);
                char del[512];
                snprintf(del, sizeof(del), "DELETE FROM event_outbox WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM transactions WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM entities WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM sessions WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM users WHERE organization_id = %lld;", org_id);
                db_client_query(client, del);
                snprintf(del, sizeof(del), "DELETE FROM organizations WHERE id = %lld;", org_id);
                db_client_query(client, del);
            }
            db_client_free_result(res);
        }
    }
}

int main(void) {
    printf("=== RUNNING TRANSACTION PROCESSING HTTP ENDPOINT TESTS ===\n");

    load_env_file(".env");
    load_env_file("../.env");

    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient client;
    if (db_client_init(&client) != 0 || db_client_connect(&client, &db_cfg) != 0) {
        printf("Database connection failed\n");
        return 1;
    }

    const char *org_name = "ProcHttpOrg";
    cleanup_org(&client, org_name);

    long long org_id = 0;
    long long admin_id = 0;
    char err_msg[256];
    auth_register_organization(&client, org_name, "admin@prochttp.com",
                               "AdminPassword123!", &org_id, &admin_id, err_msg, sizeof(err_msg));

    long long viewer_id = 0;
    auth_create_user(&client, admin_id, ROLE_ADMIN, org_id, "viewer@prochttp.com",
                     "ViewerPassword123!", ROLE_VIEWER, &viewer_id, err_msg, sizeof(err_msg));

    Entity ent_a, ent_b;
    entity_service_create(&client, org_id, admin_id, "Entity A", ENTITY_TYPE_VENDOR, &ent_a, err_msg, sizeof(err_msg));
    entity_service_create(&client, org_id, admin_id, "Entity B", ENTITY_TYPE_CLIENT, &ent_b, err_msg, sizeof(err_msg));
    long long ent_a_id = ent_a.id;
    long long ent_b_id = ent_b.id;

    char admin_session_token[128];
    char admin_csrf_token[128];
    session_create(&client, org_id, admin_id, admin_session_token, sizeof(admin_session_token),
                   admin_csrf_token, sizeof(admin_csrf_token));

    char viewer_session_token[128];
    char viewer_csrf_token[128];
    session_create(&client, org_id, viewer_id, viewer_session_token, sizeof(viewer_session_token),
                   viewer_csrf_token, sizeof(viewer_csrf_token));

    char admin_cookie[256];
    snprintf(admin_cookie, sizeof(admin_cookie), "session_token=%s", admin_session_token);

    char viewer_cookie[256];
    snprintf(viewer_cookie, sizeof(viewer_cookie), "session_token=%s", viewer_session_token);

    HttpServerConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.port = 8092;
    strncpy(cfg.app_env, "development", sizeof(cfg.app_env) - 1);
    strncpy(cfg.allowed_origins, "http://localhost:3000", sizeof(cfg.allowed_origins) - 1);
    cfg.db_client = &client;

    if (http_server_start(&cfg) != 0) {
        printf("Failed to start HTTP server on port %d\n", cfg.port);
        db_client_close(&client);
        return 1;
    }

#ifdef _WIN32
    HANDLE th = (HANDLE)_beginthreadex(NULL, 0, server_thread_func, NULL, 0, NULL);
    Sleep(200);
#else
    pthread_t th;
    pthread_create(&th, NULL, server_thread_func, NULL);
    usleep(200000);
#endif

    char resp[8192];

    send_request(cfg.port, "POST", "/api/transactions/process-next", viewer_cookie, viewer_csrf_token, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Viewer cannot process transactions (403 Forbidden)");

    send_request(cfg.port, "POST", "/api/transactions/process-next", admin_cookie, "wrong_csrf", NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Missing or invalid CSRF token rejected (403 Forbidden)");

    send_request(cfg.port, "POST", "/api/transactions/process-next", admin_cookie, admin_csrf_token, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "404 Not Found") != NULL, "Process next returns 404 when no pending transactions");

    Transaction tx1;
    int is_replay = 0;
    transaction_service_create(&client, org_id, admin_id, ent_a_id, ent_b_id,
                              "10.00", "INR", "d0000000-0000-4000-8000-000000000001", &tx1, &is_replay, err_msg, sizeof(err_msg));

    send_request(cfg.port, "POST", "/api/transactions/process-next", admin_cookie, admin_csrf_token, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"success\":true") != NULL && strstr(resp, "\"status\":\"Completed\"") != NULL,
                "Process next successfully completes pending transaction");

    Transaction tx2, tx3;
    transaction_service_create(&client, org_id, admin_id, ent_a_id, ent_b_id,
                              "20.00", "INR", "d0000000-0000-4000-8000-000000000002", &tx2, &is_replay, err_msg, sizeof(err_msg));
    transaction_service_create(&client, org_id, admin_id, ent_a_id, ent_b_id,
                              "30.00", "INR", "d0000000-0000-4000-8000-000000000003", &tx3, &is_replay, err_msg, sizeof(err_msg));

    send_request(cfg.port, "POST", "/api/transactions/process-batch", admin_cookie, admin_csrf_token, "{\"batch_size\":5}", resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"processed_count\":2") != NULL,
                "Process batch successfully processes remaining 2 pending transactions");

    send_request(cfg.port, "GET", "/api/transactions/processing/status", viewer_cookie, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"completed_count\":3") != NULL && strstr(resp, "\"pending_count\":0") != NULL,
                "GET /api/transactions/processing/status returns correct counts to Viewer");

    Transaction tx_p1, tx_p2;
    transaction_service_create(&client, org_id, admin_id, ent_a_id, ent_b_id,
                              "15.00", "INR", "d0000000-0000-4000-8000-000000000004", &tx_p1, &is_replay, err_msg, sizeof(err_msg));
    transaction_service_create(&client, org_id, admin_id, ent_a_id, ent_b_id,
                              "150.00", "INR", "d0000000-0000-4000-8000-000000000005", &tx_p2, &is_replay, err_msg, sizeof(err_msg));

    send_request(cfg.port, "GET", "/api/transactions/pending/priority?limit=10", admin_cookie, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"count\":2") != NULL, "GET /api/transactions/pending/priority returns count 2");
    char *pos_150 = strstr(resp, "150.00");
    char *pos_15 = strstr(resp, "15.00");
    EXPECT_TRUE(pos_150 != NULL && pos_15 != NULL && pos_150 < pos_15, "Priority order returns higher amount 150.00 before 15.00");

    send_request(cfg.port, "POST", "/api/transactions/process-batch", admin_cookie, admin_csrf_token, "{\"batch_size\":10}", resp, sizeof(resp));

    send_request(cfg.port, "GET", "/api/entities/balances", viewer_cookie, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"balances\":[") != NULL && strstr(resp, "\"balance_paise\":") != NULL,
                "GET /api/entities/balances returns entity balances array");

    send_request(cfg.port, "GET", "/api/settlements/recommendations", viewer_cookie, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"recommendations\":[") != NULL && strstr(resp, "\"total_settled_paise\":") != NULL,
                "GET /api/settlements/recommendations returns settlement recommendations");

    send_request(cfg.port, "GET", "/api/transactions/recent/sorted?sort_by=amount&order=desc", viewer_cookie, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"sort_by\":\"amount\"") != NULL && strstr(resp, "\"order\":\"desc\"") != NULL,
                "GET /api/transactions/recent/sorted returns sorted list");

    char search_path[128];
    snprintf(search_path, sizeof(search_path), "/api/transactions/recent/search?id=%lld", tx1.id);
    send_request(cfg.port, "GET", search_path, viewer_cookie, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL && strstr(resp, "\"success\":true") != NULL && strstr(resp, "d0000000-0000-4000-8000-000000000001") != NULL,
                "GET /api/transactions/recent/search finds transaction by ID");

    send_request(cfg.port, "GET", "/api/transactions/recent/search?id=999999999", viewer_cookie, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "404 Not Found") != NULL,
                "GET /api/transactions/recent/search returns 404 for non-existent ID");

    g_run_server = 0;
#ifdef _WIN32
    WaitForSingleObject(th, 2000);
    CloseHandle(th);
#else
    pthread_join(th, NULL);
#endif
    http_server_stop();

    cleanup_org(&client, org_name);
    db_client_close(&client);

    printf("\nHTTP Processing Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return (g_tests_failed == 0) ? 0 : 1;
}
