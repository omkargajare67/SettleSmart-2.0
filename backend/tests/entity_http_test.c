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

static int send_json_request(int port, const char *method, const char *path,
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
    snprintf(search_str, sizeof(search_str), "%s=", cookie_name);
    char *pos = strstr(resp, search_str);
    if (pos) {
        char *end = strchr(pos, ';');
        if (end) {
            size_t len = (size_t)(end - pos);
            if (len >= max_out) len = max_out - 1;
            memcpy(out, pos, len);
            out[len] = '\0';
        }
    }
}

static void extract_json_id(const char *resp, const char *key, long long *out_id) {
    *out_id = 0;
    char search_str[64];
    snprintf(search_str, sizeof(search_str), "\"%s\":", key);
    char *pos = strstr(resp, search_str);
    if (pos) {
        char *start = pos + strlen(search_str);
        *out_id = atoll(start);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Starting Entity HTTP Routes and Integration Tests...\n");

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
        printf("FAIL: Database connection failed: %s\n", db_client_get_error(&client));
        return 1;
    }

    db_client_query(&client, "DELETE FROM sessions WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'EntityHttpTest%');");
    db_client_query(&client, "DELETE FROM entities WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'EntityHttpTest%');");
    db_client_query(&client, "DELETE FROM users WHERE organization_id IN (SELECT id FROM organizations WHERE name LIKE 'EntityHttpTest%');");
    db_client_query(&client, "DELETE FROM organizations WHERE name LIKE 'EntityHttpTest%';");

    long long org_a_id = 0, admin_a_id = 0;
    long long org_b_id = 0, admin_b_id = 0;
    char err_buf[256];

    auth_register_organization(&client, "EntityHttpTest Alpha", "admin_a@enthttp.com",
                               "MasterHttpPassword#2026", &org_a_id, &admin_a_id, err_buf, sizeof(err_buf));

    auth_register_organization(&client, "EntityHttpTest Beta", "admin_b@enthttp.com",
                               "MasterHttpPassword#2026", &org_b_id, &admin_b_id, err_buf, sizeof(err_buf));

    long long fm_a_id = 0;
    auth_create_user(&client, admin_a_id, ROLE_ADMIN, org_a_id, "fm_a@enthttp.com",
                     "MasterHttpPassword#2026", ROLE_FINANCE_MANAGER, &fm_a_id, err_buf, sizeof(err_buf));

    long long viewer_a_id = 0;
    auth_create_user(&client, admin_a_id, ROLE_ADMIN, org_a_id, "viewer_a@enthttp.com",
                     "MasterHttpPassword#2026", ROLE_VIEWER, &viewer_a_id, err_buf, sizeof(err_buf));

    HttpServerConfig server_cfg;
    server_cfg.port = 18082;
    strncpy(server_cfg.allowed_origins, "http://localhost:3000, https://app.settlesmart.com", sizeof(server_cfg.allowed_origins) - 1);
    server_cfg.allowed_origins[sizeof(server_cfg.allowed_origins) - 1] = '\0';
    strncpy(server_cfg.app_env, "development", sizeof(server_cfg.app_env) - 1);
    server_cfg.app_env[sizeof(server_cfg.app_env) - 1] = '\0';
    server_cfg.cookie_secure = 0;
    server_cfg.enable_public_registration = 1;
    server_cfg.db_client = &client;

    mg_log_set(MG_LL_NONE);
    if (http_server_start(&server_cfg) != 0) {
        printf("FAIL: Could not start HTTP server on port 18082\n");
        db_client_close(&client);
        return 1;
    }

#ifdef _WIN32
    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, server_thread_func, NULL, 0, NULL);
    Sleep(150);
#else
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, server_thread_func, NULL);
    usleep(150000);
#endif

    char resp[8192];

    char login_admin_a[256];
    snprintf(login_admin_a, sizeof(login_admin_a),
             "{\"organization_id\":%lld,\"email\":\"admin_a@enthttp.com\",\"password\":\"MasterHttpPassword#2026\"}",
             org_a_id);
    send_json_request(18082, "POST", "/api/auth/login", NULL, NULL, login_admin_a, resp, sizeof(resp));
    char cookie_admin_a[128], csrf_admin_a[128];
    extract_cookie(resp, "session_token", cookie_admin_a, sizeof(cookie_admin_a));
    extract_token(resp, "csrf_token", csrf_admin_a, sizeof(csrf_admin_a));
    EXPECT_TRUE(cookie_admin_a[0] != '\0' && csrf_admin_a[0] != '\0', "Admin A logged in");

    char login_fm_a[256];
    snprintf(login_fm_a, sizeof(login_fm_a),
             "{\"organization_id\":%lld,\"email\":\"fm_a@enthttp.com\",\"password\":\"MasterHttpPassword#2026\"}",
             org_a_id);
    send_json_request(18082, "POST", "/api/auth/login", NULL, NULL, login_fm_a, resp, sizeof(resp));
    char cookie_fm_a[128], csrf_fm_a[128];
    extract_cookie(resp, "session_token", cookie_fm_a, sizeof(cookie_fm_a));
    extract_token(resp, "csrf_token", csrf_fm_a, sizeof(csrf_fm_a));
    EXPECT_TRUE(cookie_fm_a[0] != '\0' && csrf_fm_a[0] != '\0', "Finance Manager A logged in");

    char login_viewer_a[256];
    snprintf(login_viewer_a, sizeof(login_viewer_a),
             "{\"organization_id\":%lld,\"email\":\"viewer_a@enthttp.com\",\"password\":\"MasterHttpPassword#2026\"}",
             org_a_id);
    send_json_request(18082, "POST", "/api/auth/login", NULL, NULL, login_viewer_a, resp, sizeof(resp));
    char cookie_viewer_a[128], csrf_viewer_a[128];
    extract_cookie(resp, "session_token", cookie_viewer_a, sizeof(cookie_viewer_a));
    extract_token(resp, "csrf_token", csrf_viewer_a, sizeof(csrf_viewer_a));
    EXPECT_TRUE(cookie_viewer_a[0] != '\0' && csrf_viewer_a[0] != '\0', "Viewer A logged in");

    char login_admin_b[256];
    snprintf(login_admin_b, sizeof(login_admin_b),
             "{\"organization_id\":%lld,\"email\":\"admin_b@enthttp.com\",\"password\":\"MasterHttpPassword#2026\"}",
             org_b_id);
    send_json_request(18082, "POST", "/api/auth/login", NULL, NULL, login_admin_b, resp, sizeof(resp));
    char cookie_admin_b[128], csrf_admin_b[128];
    extract_cookie(resp, "session_token", cookie_admin_b, sizeof(cookie_admin_b));
    extract_token(resp, "csrf_token", csrf_admin_b, sizeof(csrf_admin_b));
    EXPECT_TRUE(cookie_admin_b[0] != '\0' && csrf_admin_b[0] != '\0', "Admin B logged in");

    send_json_request(18082, "GET", "/api/entities", NULL, NULL, NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "401") != NULL, "Unauthenticated GET /api/entities rejected with 401");

    send_json_request(18082, "POST", "/api/entities", NULL, NULL,
                      "{\"name\":\"Ghost Entity\",\"entity_type\":\"Vendor\"}", resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "401") != NULL, "Unauthenticated POST /api/entities rejected with 401");

    send_json_request(18082, "POST", "/api/entities", cookie_admin_a, NULL,
                      "{\"name\":\"No CSRF Entity\",\"entity_type\":\"Vendor\"}", resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403") != NULL, "POST /api/entities without CSRF header rejected with 403");

    send_json_request(18082, "POST", "/api/entities", cookie_admin_a, "bogus_token",
                      "{\"name\":\"Bad CSRF Entity\",\"entity_type\":\"Vendor\"}", resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403") != NULL, "POST /api/entities with invalid CSRF token rejected with 403");

    const char *create_body_1 = "{\"name\":\"Global Logistics\",\"entity_type\":\"Vendor\"}";
    send_json_request(18082, "POST", "/api/entities", cookie_admin_a, csrf_admin_a,
                      create_body_1, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "201 Created") != NULL, "Admin A POST /api/entities succeeds (201 Created)");
    EXPECT_TRUE(strstr(resp, "\"name\":\"Global Logistics\"") != NULL, "Response contains created entity name");
    EXPECT_TRUE(strstr(resp, "\"entity_type\":\"Vendor\"") != NULL, "Response contains created entity type");

    long long entity_1_id = 0;
    extract_json_id(resp, "id", &entity_1_id);
    EXPECT_TRUE(entity_1_id > 0, "Created entity ID extracted");

    const char *create_dup_body = "{\"name\":\"Global Logistics\",\"entity_type\":\"Department\"}";
    send_json_request(18082, "POST", "/api/entities", cookie_admin_a, csrf_admin_a,
                      create_dup_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "409 Conflict") != NULL, "Duplicate entity name returns 409 Conflict");
    EXPECT_TRUE(strstr(resp, "already exists in organization") != NULL, "Conflict error message returned");

    const char *fm_create_body = "{\"name\":\"Finance Dept\",\"entity_type\":\"Department\"}";
    send_json_request(18082, "POST", "/api/entities", cookie_fm_a, csrf_fm_a,
                      fm_create_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "201 Created") != NULL, "Finance Manager A can create entities (201 Created)");

    long long entity_2_id = 0;
    extract_json_id(resp, "id", &entity_2_id);
    EXPECT_TRUE(entity_2_id > 0, "Entity 2 ID extracted");

    const char *patch_body = "{\"name\":\"Global Logistics International\",\"entity_type\":\"Partner\"}";
    char patch_path[64];
    snprintf(patch_path, sizeof(patch_path), "/api/entities/%lld", entity_1_id);
    send_json_request(18082, "PATCH", patch_path, cookie_fm_a, csrf_fm_a,
                      patch_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Finance Manager A can update entity details (200 OK)");
    EXPECT_TRUE(strstr(resp, "\"name\":\"Global Logistics International\"") != NULL, "Entity name updated");
    EXPECT_TRUE(strstr(resp, "\"entity_type\":\"Partner\"") != NULL, "Entity type updated");

    const char *status_body = "{\"status\":\"Inactive\"}";
    char status_path[64];
    snprintf(status_path, sizeof(status_path), "/api/entities/%lld/status", entity_1_id);
    send_json_request(18082, "PATCH", status_path, cookie_admin_a, csrf_admin_a,
                      status_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Admin A can deactivate entity status (200 OK)");
    EXPECT_TRUE(strstr(resp, "\"status\":\"Inactive\"") != NULL, "Entity status set to Inactive");

    send_json_request(18082, "GET", "/api/entities?page=1&limit=10", cookie_viewer_a, NULL,
                      NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Viewer A can list entities (200 OK)");
    EXPECT_TRUE(strstr(resp, "\"total_records\":2") != NULL, "List contains 2 entities for Org A");

    char get_one_path[64];
    snprintf(get_one_path, sizeof(get_one_path), "/api/entities/%lld", entity_1_id);
    send_json_request(18082, "GET", get_one_path, cookie_viewer_a, NULL,
                      NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "200 OK") != NULL, "Viewer A can get single entity by ID (200 OK)");

    send_json_request(18082, "POST", "/api/entities", cookie_viewer_a, csrf_viewer_a,
                      "{\"name\":\"Unauthorized Entity\",\"entity_type\":\"Vendor\"}", resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Viewer A POST /api/entities rejected with 403 Forbidden");

    send_json_request(18082, "PATCH", patch_path, cookie_viewer_a, csrf_viewer_a,
                      patch_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Viewer A PATCH /api/entities rejected with 403 Forbidden");

    send_json_request(18082, "PATCH", status_path, cookie_viewer_a, csrf_viewer_a,
                      status_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "403 Forbidden") != NULL, "Viewer A PATCH status rejected with 403 Forbidden");

    send_json_request(18082, "GET", get_one_path, cookie_admin_b, NULL,
                      NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "404 Not Found") != NULL, "Cross-organization GET returns 404 Not Found");
    EXPECT_TRUE(strstr(resp, "Entity not found") != NULL, "Generic error message masks existence");

    send_json_request(18082, "PATCH", patch_path, cookie_admin_b, csrf_admin_b,
                      patch_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "404 Not Found") != NULL, "Cross-organization PATCH returns 404 Not Found");

    send_json_request(18082, "PATCH", status_path, cookie_admin_b, csrf_admin_b,
                      status_body, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "404 Not Found") != NULL, "Cross-organization status update returns 404 Not Found");

    send_json_request(18082, "DELETE", get_one_path, cookie_admin_a, csrf_admin_a,
                      NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "405 Method Not Allowed") != NULL, "DELETE /api/entities/:id returns 405 Method Not Allowed");
    EXPECT_TRUE(strstr(resp, "Physical deletion of entities is not permitted") != NULL, "Physical deletion rejection message returned");

    send_json_request(18082, "GET", "/api/entities/999999", cookie_admin_a, NULL,
                      NULL, resp, sizeof(resp));
    EXPECT_TRUE(strstr(resp, "404 Not Found") != NULL, "Nonexistent entity lookup returns 404 Not Found");

    g_run_server = 0;
#ifdef _WIN32
    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);
#else
    pthread_join(thread_id, NULL);
#endif
    http_server_stop();

    db_client_close(&client);

    printf("\nEntity HTTP Integration Tests Completed. Passed: %d, Failed: %d\n",
           g_tests_passed, g_tests_failed);

    return g_tests_failed == 0 ? 0 : 1;
}
