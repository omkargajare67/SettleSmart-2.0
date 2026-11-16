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

static int send_tx_request(int port, const char *method, const char *path,
                           const char *cookie, const char *csrf, const char *idem_key,
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
    if (idem_key && strlen(idem_key) > 0) {
        snprintf(headers + strlen(headers), sizeof(headers) - strlen(headers),
                 "Idempotency-Key: %s\r\n", idem_key);
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

static void extract_token(const char *resp, const char *key, char *out, size_t max_out) {
    out[0] = '\0';
    char search_str[64];
    snprintf(search_str, sizeof(search_str), "\"%s\":\"", key);
    char *pos = strstr(resp, search_str);
    if (pos) {
        char *start = pos + strlen(search_str);
        char *end = strchr(start, '\"');
        if (end) {
            size_t len = (size_t)(end - start);
            if (len >= max_out) len = max_out - 1;
            memcpy(out, start, len);
            out[len] = '\0';
        }
    }
}

static void extract_cookie(const char *resp, const char *cookie_name, char *out, size_t max_out) {
    out[0] = '\0';
    char search_str[64];
    snprintf(search_str, sizeof(search_str), "Set-Cookie: %s=", cookie_name);
    char *pos = strstr(resp, search_str);
    if (pos) {
        char *start = pos + strlen("Set-Cookie: ");
        char *end = strstr(start, ";");
        if (end) {
            size_t len = (size_t)(end - start);
            if (len >= max_out) len = max_out - 1;
            memcpy(out, start, len);
            out[len] = '\0';
        }
    }
}

static long long extract_json_id(const char *resp) {
    char *pos = strstr(resp, "\"id\":");
    if (pos) {
        return atoll(pos + 5);
    }
    return 0;
}

int main(void) {
    printf("Starting Transaction HTTP Endpoint Integration Tests...\n");

    load_env_file(".env");
    load_env_file("../.env");

    DBConfig db_cfg;
    db_cfg.host = get_env_value("DB_HOST", "127.0.0.1");
    db_cfg.port = get_env_int("DB_PORT", 3307);
    db_cfg.database = get_env_value("DB_NAME", "settlesmart_test");
    db_cfg.user = get_env_value("DB_USER", "settlesmart_user");
    db_cfg.password = get_env_value("DB_PASSWORD", "dev_password_123");
    db_cfg.ssl_mode = get_env_value("DB_SSL_MODE", "preferred");

    DBClient db_client;
    if (db_client_init(&db_client) != 0 || db_client_connect(&db_client, &db_cfg) != 0) {
        printf("FAIL: Database connection failed: %s\n", db_client_get_error(&db_client));
        return 1;
    }

    db_client_query(&db_client, "UPDATE transactions SET reversal_of_id = NULL WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx HTTP Org%');");
    db_client_query(&db_client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx HTTP Org%');");
    db_client_query(&db_client, "DELETE FROM transactions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx HTTP Org%');");
    db_client_query(&db_client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx HTTP Org%');");
    db_client_query(&db_client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'Tx HTTP Org%');");
    db_client_query(&db_client, "DELETE FROM organizations WHERE name LIKE 'Tx HTTP Org%';");

    long long org1_id = 0, admin1_id = 0;
    long long org2_id = 0, admin2_id = 0;
    char err_buf[256];

    auth_register_organization(&db_client, "Tx HTTP Org 1", "tx_admin1@org1.com",
                               "SecureAdmin123!", &org1_id, &admin1_id, err_buf, sizeof(err_buf));

    auth_register_organization(&db_client, "Tx HTTP Org 2", "tx_admin2@org2.com",
                               "SecureAdmin123!", &org2_id, &admin2_id, err_buf, sizeof(err_buf));

    long long fm1_id = 0;
    auth_create_user(&db_client, admin1_id, ROLE_ADMIN, org1_id, "tx_fm1@org1.com",
                     "SecureFM123!", ROLE_FINANCE_MANAGER, &fm1_id, err_buf, sizeof(err_buf));

    long long viewer1_id = 0;
    auth_create_user(&db_client, admin1_id, ROLE_ADMIN, org1_id, "tx_viewer1@org1.com",
                     "SecureViewer123!", ROLE_VIEWER, &viewer1_id, err_buf, sizeof(err_buf));

    int test_port = 18095;
    HttpServerConfig srv_cfg;
    memset(&srv_cfg, 0, sizeof(srv_cfg));
    srv_cfg.port = test_port;
    strncpy(srv_cfg.allowed_origins, "http://localhost:3000", sizeof(srv_cfg.allowed_origins) - 1);
    strncpy(srv_cfg.app_env, "development", sizeof(srv_cfg.app_env) - 1);
    srv_cfg.cookie_secure = 0;
    srv_cfg.enable_public_registration = 1;
    srv_cfg.db_client = &db_client;

    mg_log_set(MG_LL_NONE);
    if (http_server_start(&srv_cfg) != 0) {
        printf("FAIL: Failed to initialize HTTP server\n");
        return 1;
    }

#ifdef _WIN32
    HANDLE h_srv = (HANDLE)_beginthreadex(NULL, 0, server_thread_func, NULL, 0, NULL);
    Sleep(150);
#else
    pthread_t th_srv;
    pthread_create(&th_srv, NULL, server_thread_func, NULL);
    usleep(150000);
#endif

    char resp[16384];

    char admin_cookie[256];
    char admin_csrf[128];
    char login_admin1[256];
    snprintf(login_admin1, sizeof(login_admin1),
             "{\"organization_id\":%lld,\"email\":\"tx_admin1@org1.com\",\"password\":\"SecureAdmin123!\"}",
             org1_id);
    send_tx_request(test_port, "POST", "/api/auth/login", NULL, NULL, NULL,
                    login_admin1, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Login Admin 1");
    extract_cookie(resp, "session_token", admin_cookie, sizeof(admin_cookie));
    extract_token(resp, "csrf_token", admin_csrf, sizeof(admin_csrf));

    char org2_cookie[256];
    char org2_csrf[128];
    char login_admin2[256];
    snprintf(login_admin2, sizeof(login_admin2),
             "{\"organization_id\":%lld,\"email\":\"tx_admin2@org2.com\",\"password\":\"SecureAdmin123!\"}",
             org2_id);
    send_tx_request(test_port, "POST", "/api/auth/login", NULL, NULL, NULL,
                    login_admin2, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Login Admin 2");
    extract_cookie(resp, "session_token", org2_cookie, sizeof(org2_cookie));
    extract_token(resp, "csrf_token", org2_csrf, sizeof(org2_csrf));

    char fm_cookie[256];
    char fm_csrf[128];
    char login_fm[256];
    snprintf(login_fm, sizeof(login_fm),
             "{\"organization_id\":%lld,\"email\":\"tx_fm1@org1.com\",\"password\":\"SecureFM123!\"}",
             org1_id);
    send_tx_request(test_port, "POST", "/api/auth/login", NULL, NULL, NULL,
                    login_fm, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Login Finance Manager");
    extract_cookie(resp, "session_token", fm_cookie, sizeof(fm_cookie));
    extract_token(resp, "csrf_token", fm_csrf, sizeof(fm_csrf));

    char viewer_cookie[256];
    char viewer_csrf[128];
    char login_viewer[256];
    snprintf(login_viewer, sizeof(login_viewer),
             "{\"organization_id\":%lld,\"email\":\"tx_viewer1@org1.com\",\"password\":\"SecureViewer123!\"}",
             org1_id);
    send_tx_request(test_port, "POST", "/api/auth/login", NULL, NULL, NULL,
                    login_viewer, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Login Viewer");
    extract_cookie(resp, "session_token", viewer_cookie, sizeof(viewer_cookie));
    extract_token(resp, "csrf_token", viewer_csrf, sizeof(viewer_csrf));

    send_tx_request(test_port, "POST", "/api/entities", admin_cookie, admin_csrf, NULL,
                    "{\"name\":\"HTTP Ent Alpha\",\"entity_type\":\"Subsidiary\"}",
                    resp, sizeof(resp));
    long long ent_alpha = extract_json_id(resp);

    send_tx_request(test_port, "POST", "/api/entities", admin_cookie, admin_csrf, NULL,
                    "{\"name\":\"HTTP Ent Beta\",\"entity_type\":\"Vendor\"}",
                    resp, sizeof(resp));
    long long ent_beta = extract_json_id(resp);
    EXPECT_TRUE(ent_alpha > 0 && ent_beta > 0, "Entities Alpha and Beta created");

    char tx_payload[512];
    snprintf(tx_payload, sizeof(tx_payload),
             "{\"sender_entity_id\":%lld,\"receiver_entity_id\":%lld,\"amount\":\"1250.75\",\"currency\":\"INR\"}",
             ent_alpha, ent_beta);

    send_tx_request(test_port, "POST", "/api/transactions", NULL, NULL, "10000000-0000-4000-8000-000000000001",
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "401 Unauthorized") != NULL, "Unauthenticated transaction creation returns 401");

    send_tx_request(test_port, "GET", "/api/transactions", NULL, NULL, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "401 Unauthorized") != NULL, "Unauthenticated transaction listing returns 401");

    send_tx_request(test_port, "POST", "/api/transactions", viewer_cookie, viewer_csrf, "10000000-0000-4000-8000-000000000002",
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Viewer creation returns 403 Forbidden");

    send_tx_request(test_port, "POST", "/api/transactions", admin_cookie, NULL, "10000000-0000-4000-8000-000000000003",
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Missing CSRF token returns 403 Forbidden");

    send_tx_request(test_port, "POST", "/api/transactions", admin_cookie, "invalid_csrf_val", "10000000-0000-4000-8000-000000000004",
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Invalid CSRF token returns 403 Forbidden");

    send_tx_request(test_port, "POST", "/api/transactions", admin_cookie, admin_csrf, NULL,
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "400 Bad Request") != NULL, "Missing Idempotency-Key header returns 400 Bad Request");

    send_tx_request(test_port, "POST", "/api/transactions", admin_cookie, admin_csrf, "not-a-valid-uuid",
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "400 Bad Request") != NULL, "Invalid UUID v4 Idempotency-Key returns 400 Bad Request");

    const char *key_fm = "20000000-0000-4000-8000-000000000001";
    send_tx_request(test_port, "POST", "/api/transactions", fm_cookie, fm_csrf, key_fm,
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "201 Created") != NULL, "Finance Manager can create transaction (201 Created)");
    EXPECT_TRUE(strstr(resp, "\"idempotent_replay\":false") != NULL, "idempotent_replay is false for initial create");
    long long tx_created_id = extract_json_id(resp);
    EXPECT_TRUE(tx_created_id > 0, "Created transaction received valid ID");

    send_tx_request(test_port, "POST", "/api/transactions", fm_cookie, fm_csrf, key_fm,
                    tx_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Idempotent replay returns 200 OK");
    EXPECT_TRUE(strstr(resp, "\"idempotent_replay\":true") != NULL, "idempotent_replay is true");
    long long replay_id = extract_json_id(resp);
    EXPECT_TRUE(replay_id == tx_created_id, "Replay ID matches original ID");

    char tx_diff_payload[512];
    snprintf(tx_diff_payload, sizeof(tx_diff_payload),
             "{\"sender_entity_id\":%lld,\"receiver_entity_id\":%lld,\"amount\":\"9999.00\",\"currency\":\"INR\"}",
             ent_alpha, ent_beta);
    send_tx_request(test_port, "POST", "/api/transactions", fm_cookie, fm_csrf, key_fm,
                    tx_diff_payload, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "409 Conflict") != NULL, "Conflicting payload with same idempotency key returns 409 Conflict");

    char get_path[128];
    snprintf(get_path, sizeof(get_path), "/api/transactions/%lld", tx_created_id);

    send_tx_request(test_port, "GET", get_path, viewer_cookie, NULL, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Viewer can view transaction details (200 OK)");

    send_tx_request(test_port, "GET", "/api/transactions", viewer_cookie, NULL, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Viewer can list transactions (200 OK)");

    send_tx_request(test_port, "GET", "/api/transactions/recent", viewer_cookie, NULL, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Viewer can get recent transactions (200 OK)");

    send_tx_request(test_port, "PATCH", get_path, admin_cookie, admin_csrf, NULL, "{\"amount\":\"500.00\"}", resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "405 Method Not Allowed") != NULL, "PATCH transaction returns 405 Method Not Allowed");
    EXPECT_TRUE(strstr(resp, "immutable") != NULL, "PATCH response specifies transaction immutability");

    send_tx_request(test_port, "DELETE", get_path, admin_cookie, admin_csrf, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "405 Method Not Allowed") != NULL, "DELETE transaction returns 405 Method Not Allowed");
    EXPECT_TRUE(strstr(resp, "immutable") != NULL, "DELETE response specifies transaction immutability");

    send_tx_request(test_port, "GET", get_path, org2_cookie, NULL, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "404 Not Found") != NULL, "Cross-organization get transaction returns 404 Not Found");

    char rev_path[128];
    snprintf(rev_path, sizeof(rev_path), "/api/transactions/%lld/reversal", tx_created_id);

    send_tx_request(test_port, "POST", rev_path, viewer_cookie, viewer_csrf, "30000000-0000-4000-8000-000000000001",
                    NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Viewer reversal returns 403 Forbidden");

    char mark_complete[256];
    snprintf(mark_complete, sizeof(mark_complete),
             "UPDATE transactions SET status = 'Completed' WHERE id = %lld", tx_created_id);
    db_client_query(&db_client, mark_complete);

    const char *key_rev = "40000000-0000-4000-8000-000000000001";
    send_tx_request(test_port, "POST", rev_path, admin_cookie, admin_csrf, key_rev,
                    NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "201 Created") != NULL, "Admin can create reversal (201 Created)");
    EXPECT_TRUE(strstr(resp, "\"type\":\"Reversal\"") != NULL, "Created transaction has type Reversal");

    send_tx_request(test_port, "POST", rev_path, admin_cookie, admin_csrf, key_rev,
                    NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Idempotent reversal retry returns 200 OK");
    EXPECT_TRUE(strstr(resp, "\"idempotent_replay\":true") != NULL, "Reversal marked as idempotent replay");

    send_tx_request(test_port, "POST", rev_path, admin_cookie, admin_csrf, "40000000-0000-4000-8000-000000000002",
                    NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "409 Conflict") != NULL, "Second reversal attempt returns 409 Conflict");

    g_run_server = 0;
#ifdef _WIN32
    WaitForSingleObject(h_srv, 1000);
    CloseHandle(h_srv);
#else
    pthread_join(th_srv, NULL);
#endif

    http_server_stop();
    db_client_close(&db_client);

    printf("Transaction HTTP Test Complete: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
