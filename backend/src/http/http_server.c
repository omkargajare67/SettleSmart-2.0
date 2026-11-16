#include "http_server.h"
#include "entity_routes.h"
#include "transaction_routes.h"
#include "graph_routes.h"
#include "processing_routes.h"
#include "../websocket/websocket.h"
#include "../websocket/outbox_consumer.h"
#include "../auth/crypto_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct mg_mgr g_http_mgr;
static int g_server_running = 0;
static OutboxConsumer g_http_outbox_consumer;
static int g_consumer_active = 0;

int http_is_origin_allowed(const char *allowed_origins, const char *origin) {
    if (!origin || strlen(origin) == 0 || !allowed_origins || strlen(allowed_origins) == 0) {
        return 0;
    }

    char copy[512];
    strncpy(copy, allowed_origins, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *token = strtok(copy, ",");
    while (token) {
        while (*token == ' ') {
            token++;
        }
        size_t len = strlen(token);
        while (len > 0 && token[len - 1] == ' ') {
            token[len - 1] = '\0';
            len--;
        }
        if (strcmp(token, origin) == 0 || strcmp(token, "*") == 0) {
            return 1;
        }
        token = strtok(NULL, ",");
    }
    return 0;
}

int http_parse_cookie(const char *cookie_str, size_t cookie_len,
                      const char *cookie_name, char *out_val, size_t max_out) {
    if (!cookie_str || !cookie_name || !out_val || max_out == 0) {
        return -1;
    }

    size_t name_len = strlen(cookie_name);
    const char *curr = cookie_str;
    const char *end = cookie_str + cookie_len;

    while (curr < end) {
        while (curr < end && (*curr == ' ' || *curr == ';')) {
            curr++;
        }
        if (curr + name_len + 1 <= end &&
            strncmp(curr, cookie_name, name_len) == 0 &&
            curr[name_len] == '=') {
            const char *val_start = curr + name_len + 1;
            const char *val_end = val_start;
            while (val_end < end && *val_end != ';') {
                val_end++;
            }
            size_t val_len = (size_t)(val_end - val_start);
            if (val_len >= max_out) {
                val_len = max_out - 1;
            }
            memcpy(out_val, val_start, val_len);
            out_val[val_len] = '\0';
            return 0;
        }
        while (curr < end && *curr != ';') {
            curr++;
        }
    }
    return -1;
}

void http_send_json_reply(struct mg_connection *c, int status_code,
                          const char *origin, const HttpServerConfig *config,
                          const char *extra_headers, const char *json_body) {
    char headers[1024];
    char cors_headers[512] = {0};

    if (origin && config && http_is_origin_allowed(config->allowed_origins, origin)) {
        snprintf(cors_headers, sizeof(cors_headers),
                 "Access-Control-Allow-Origin: %s\r\n"
                 "Access-Control-Allow-Credentials: true\r\n",
                 origin);
    }

    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "%s"
             "%s",
             cors_headers,
             extra_headers ? extra_headers : "");
    mg_http_reply(c, status_code, headers, "%s", json_body);
}

static void send_json_reply(struct mg_connection *c, int status_code,
                            const char *origin, const HttpServerConfig *config,
                            const char *extra_headers, const char *json_body) {
    http_send_json_reply(c, status_code, origin, config, extra_headers, json_body);
}

int http_extract_authenticated_session(struct mg_http_message *hm,
                                      DBClient *db_client,
                                      SessionInfo *out_session,
                                      char *raw_session_token,
                                      size_t raw_token_len) {
    struct mg_str *cookie_hdr = mg_http_get_header(hm, "Cookie");
    if (!cookie_hdr || cookie_hdr->len == 0) {
        return -1;
    }

    if (http_parse_cookie(cookie_hdr->buf, cookie_hdr->len, "session_token",
                          raw_session_token, raw_token_len) != 0) {
        return -1;
    }

    return session_validate(db_client, raw_session_token, out_session);
}

static int extract_authenticated_session(struct mg_http_message *hm,
                                         DBClient *db_client,
                                         SessionInfo *out_session,
                                         char *raw_session_token,
                                         size_t raw_token_len) {
    return http_extract_authenticated_session(hm, db_client, out_session, raw_session_token, raw_token_len);
}

int http_verify_csrf_header(struct mg_http_message *hm, const SessionInfo *session) {
    struct mg_str *csrf_hdr = mg_http_get_header(hm, "X-CSRF-Token");
    if (!csrf_hdr || csrf_hdr->len == 0) {
        return 0;
    }

    char raw_csrf[128];
    size_t copy_len = csrf_hdr->len < sizeof(raw_csrf) - 1 ? csrf_hdr->len : sizeof(raw_csrf) - 1;
    memcpy(raw_csrf, csrf_hdr->buf, copy_len);
    raw_csrf[copy_len] = '\0';

    return session_validate_csrf(session, raw_csrf);
}

static int verify_csrf_header(struct mg_http_message *hm, const SessionInfo *session) {
    return http_verify_csrf_header(hm, session);
}

void http_handle_request(struct mg_connection *c, struct mg_http_message *hm,
                         HttpServerConfig *config) {
    char req_origin[256] = {0};
    struct mg_str *orig_hdr = mg_http_get_header(hm, "Origin");
    if (orig_hdr && orig_hdr->len > 0) {
        size_t olen = orig_hdr->len < sizeof(req_origin) - 1 ? orig_hdr->len : sizeof(req_origin) - 1;
        memcpy(req_origin, orig_hdr->buf, olen);
        req_origin[olen] = '\0';
    }
    const char *active_origin = req_origin[0] ? req_origin : NULL;

    if (mg_strcmp(hm->method, mg_str("OPTIONS")) == 0) {
        if (!active_origin || !http_is_origin_allowed(config->allowed_origins, active_origin)) {
            mg_http_reply(c, 403, "Content-Type: application/json\r\n", "{\"success\":false,\"error\":\"Origin not allowed\"}");
            return;
        }

        char preflight[512];
        snprintf(preflight, sizeof(preflight),
                 "Access-Control-Allow-Origin: %s\r\n"
                 "Access-Control-Allow-Credentials: true\r\n"
                 "Access-Control-Allow-Methods: GET, POST, PATCH, PUT, DELETE, OPTIONS\r\n"
                 "Access-Control-Allow-Headers: Content-Type, X-CSRF-Token, Idempotency-Key\r\n",
                 active_origin);
        mg_http_reply(c, 204, preflight, "");
        return;
    }

    if (mg_strcmp(hm->method, mg_str("POST")) == 0 ||
        mg_strcmp(hm->method, mg_str("PATCH")) == 0 ||
        mg_strcmp(hm->method, mg_str("DELETE")) == 0 ||
        mg_strcmp(hm->method, mg_str("PUT")) == 0) {
        if (active_origin && !http_is_origin_allowed(config->allowed_origins, active_origin)) {
            send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Origin not allowed\"}");
            return;
        }
    }

    if (mg_match(hm->uri, mg_str("/ws/graph"), NULL)) {
        ws_handle_upgrade(c, hm, config->allowed_origins, config->db_client);
        return;
    }

    if (processing_routes_handle(c, hm, config, active_origin)) {
        return;
    }

    if (mg_match(hm->uri, mg_str("/api/graph#"), NULL)) {
        if (graph_routes_handle(c, hm, config, active_origin)) {
            return;
        }
    }

    if (mg_match(hm->uri, mg_str("/api/entities#"), NULL)) {
        if (entity_routes_handle(c, hm, config, active_origin)) {
            return;
        }
    }

    if (mg_match(hm->uri, mg_str("/api/transactions#"), NULL)) {
        if (transaction_routes_handle(c, hm, config, active_origin)) {
            return;
        }
    }

    if (mg_match(hm->uri, mg_str("/api/auth/register-organization"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
            send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return;
        }

        if (!config->enable_public_registration) {
            send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Public organization registration is disabled\"}");
            return;
        }

        char *org_name = mg_json_get_str(hm->body, "$.organization_name");
        char *email = mg_json_get_str(hm->body, "$.admin_email");
        char *password = mg_json_get_str(hm->body, "$.admin_password");

        if (!org_name || !email || !password) {
            free(org_name);
            free(email);
            free(password);
            send_json_reply(c, 400, active_origin, config, NULL, "{\"success\":false,\"error\":\"Missing required registration fields\"}");
            return;
        }

        long long org_id = 0;
        long long user_id = 0;
        char err_msg[256] = {0};

        int res = auth_register_organization(config->db_client, org_name, email, password,
                                             &org_id, &user_id, err_msg, sizeof(err_msg));
        free(org_name);
        free(email);
        free(password);

        if (res != 0) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            send_json_reply(c, 400, active_origin, config, NULL, resp);
            return;
        }

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"organization_id\":%lld,\"admin_user_id\":%lld}",
                 org_id, user_id);
        send_json_reply(c, 201, active_origin, config, NULL, resp);
        return;
    }

    if (mg_match(hm->uri, mg_str("/api/auth/login"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
            send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return;
        }

        long org_id = mg_json_get_long(hm->body, "$.organization_id", 0);
        char *email = mg_json_get_str(hm->body, "$.email");
        char *password = mg_json_get_str(hm->body, "$.password");

        if (org_id <= 0 || !email || !password) {
            free(email);
            free(password);
            send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Invalid organization ID, email or password\"}");
            return;
        }

        char session_token[128];
        char csrf_token[128];
        AuthUserInfo user_info;
        char err_msg[256] = {0};

        int res = auth_login(config->db_client, org_id, email, password,
                             session_token, sizeof(session_token),
                             csrf_token, sizeof(csrf_token),
                             &user_info, err_msg, sizeof(err_msg));
        free(email);
        free(password);

        if (res != 0) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            send_json_reply(c, 401, active_origin, config, NULL, resp);
            return;
        }

        char cookie_header[256];
        if (config->cookie_secure) {
            snprintf(cookie_header, sizeof(cookie_header),
                     "Set-Cookie: session_token=%s; Path=/; HttpOnly; Secure; SameSite=None\r\n",
                     session_token);
        } else {
            snprintf(cookie_header, sizeof(cookie_header),
                     "Set-Cookie: session_token=%s; Path=/; HttpOnly; SameSite=Lax\r\n",
                     session_token);
        }

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"csrf_token\":\"%s\",\"token\":\"%s\",\"user\":{\"id\":%lld,\"organization_id\":%lld,\"email\":\"%s\",\"role\":\"%s\",\"status\":\"%s\"}}",
                 csrf_token, session_token, user_info.id, user_info.organization_id, user_info.email,
                 auth_role_to_string(user_info.role), user_info.status);
        send_json_reply(c, 200, active_origin, config, cookie_header, resp);
        return;
    }

    if (mg_match(hm->uri, mg_str("/api/auth/csrf"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
            send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return;
        }

        if (active_origin && !http_is_origin_allowed(config->allowed_origins, active_origin)) {
            send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Origin not allowed\"}");
            return;
        }

        SessionInfo session;
        char raw_session_token[128];
        if (extract_authenticated_session(hm, config->db_client, &session,
                                          raw_session_token, sizeof(raw_session_token)) != 0) {
            send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return;
        }

        char new_csrf[128];
        if (session_rotate_csrf_token(config->db_client, session.session_id, new_csrf, sizeof(new_csrf)) != 0) {
            send_json_reply(c, 500, active_origin, config, NULL, "{\"success\":false,\"error\":\"Failed to generate CSRF token\"}");
            return;
        }

        char resp[256];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"csrf_token\":\"%s\"}", new_csrf);
        send_json_reply(c, 200, active_origin, config, NULL, resp);
        return;
    }

    SessionInfo session;
    char raw_session_token[128];
    if (extract_authenticated_session(hm, config->db_client, &session,
                                      raw_session_token, sizeof(raw_session_token)) != 0) {
        send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
        return;
    }

    if (mg_strcmp(hm->method, mg_str("POST")) == 0 ||
        mg_strcmp(hm->method, mg_str("PATCH")) == 0 ||
        mg_strcmp(hm->method, mg_str("DELETE")) == 0 ||
        mg_strcmp(hm->method, mg_str("PUT")) == 0) {
        if (!verify_csrf_header(hm, &session)) {
            send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
            return;
        }
    }

    if (mg_match(hm->uri, mg_str("/api/auth/logout"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
            send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return;
        }

        session_revoke(config->db_client, raw_session_token);
        char clear_cookie[256];
        if (config->cookie_secure) {
            snprintf(clear_cookie, sizeof(clear_cookie),
                     "Set-Cookie: session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; Secure; SameSite=Lax\r\n");
        } else {
            snprintf(clear_cookie, sizeof(clear_cookie),
                     "Set-Cookie: session_token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT; HttpOnly; SameSite=Lax\r\n");
        }
        send_json_reply(c, 200, active_origin, config, clear_cookie, "{\"success\":true,\"message\":\"Logged out successfully\"}");
        return;
    }

    if (mg_match(hm->uri, mg_str("/api/auth/me"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return;
        }

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"token\":\"%s\",\"user\":{\"id\":%lld,\"organization_id\":%lld,\"email\":\"%s\",\"role\":\"%s\"}}",
                 raw_session_token, session.user_id, session.organization_id, session.user_email,
                 auth_role_to_string(session.user_role));
        send_json_reply(c, 200, active_origin, config, NULL, resp);
        return;
    }

    if (mg_match(hm->uri, mg_str("/api/auth/change-password"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
            send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return;
        }

        char *old_pwd = mg_json_get_str(hm->body, "$.old_password");
        char *new_pwd = mg_json_get_str(hm->body, "$.new_password");

        if (!old_pwd || !new_pwd) {
            free(old_pwd);
            free(new_pwd);
            send_json_reply(c, 400, active_origin, config, NULL, "{\"success\":false,\"error\":\"Missing password fields\"}");
            return;
        }

        char err_msg[256] = {0};
        int res = auth_change_password(config->db_client, session.organization_id, session.user_id,
                                       old_pwd, new_pwd, err_msg, sizeof(err_msg));
        free(old_pwd);
        free(new_pwd);

        if (res != 0) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            send_json_reply(c, 400, active_origin, config, NULL, resp);
            return;
        }

        send_json_reply(c, 200, active_origin, config, NULL, "{\"success\":true,\"message\":\"Password updated successfully\"}");
        return;
    }

    if (mg_match(hm->uri, mg_str("/api/organization/users"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            char query[256];
            snprintf(query, sizeof(query),
                     "SELECT id, email, role, status FROM users WHERE organization_id = %lld ORDER BY id ASC;",
                     session.organization_id);

            if (db_client_query(config->db_client, query) != 0) {
                send_json_reply(c, 500, active_origin, config, NULL, "{\"success\":false,\"error\":\"Failed to query users\"}");
                return;
            }

            MYSQL_RES *res = db_client_store_result(config->db_client);
            if (!res) {
                send_json_reply(c, 500, active_origin, config, NULL, "{\"success\":false,\"error\":\"Failed to store result\"}");
                return;
            }

            char users_json[4096];
            snprintf(users_json, sizeof(users_json), "{\"success\":true,\"users\":[");
            MYSQL_ROW row;
            int first = 1;
            while ((row = mysql_fetch_row(res))) {
                char entry[512];
                snprintf(entry, sizeof(entry),
                         "%s{\"id\":%s,\"email\":\"%s\",\"role\":\"%s\",\"status\":\"%s\"}",
                         first ? "" : ",",
                         row[0] ? row[0] : "0",
                         row[1] ? row[1] : "",
                         row[2] ? row[2] : "",
                         row[3] ? row[3] : "");
                first = 0;
                strncat(users_json, entry, sizeof(users_json) - strlen(users_json) - 1);
            }
            strncat(users_json, "]}", sizeof(users_json) - strlen(users_json) - 1);
            db_client_free_result(res);

            send_json_reply(c, 200, active_origin, config, NULL, users_json);
            return;
        }

        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            if (!auth_can_manage_users(session.user_role)) {
                send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Permission denied: Administrator role required\"}");
                return;
            }

            char *email = mg_json_get_str(hm->body, "$.email");
            char *password = mg_json_get_str(hm->body, "$.password");
            char *role_str = mg_json_get_str(hm->body, "$.role");

            if (!email || !password || !role_str) {
                free(email);
                free(password);
                free(role_str);
                send_json_reply(c, 400, active_origin, config, NULL, "{\"success\":false,\"error\":\"Missing required user creation fields\"}");
                return;
            }

            UserRole new_role = auth_role_from_string(role_str);
            long long new_user_id = 0;
            char err_msg[256] = {0};

            int res = auth_create_user(config->db_client, session.user_id, session.user_role,
                                       session.organization_id, email, password,
                                       new_role, &new_user_id, err_msg, sizeof(err_msg));
            free(email);
            free(password);
            free(role_str);

            if (res != 0) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                send_json_reply(c, 400, active_origin, config, NULL, resp);
                return;
            }

            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":true,\"user_id\":%lld}", new_user_id);
            send_json_reply(c, 201, active_origin, config, NULL, resp);
            return;
        }

        send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
        return;
    }

    struct mg_str caps[2];
    if (mg_match(hm->uri, mg_str("/api/organization/users/*"), caps)) {
        if (mg_strcmp(hm->method, mg_str("PATCH")) != 0) {
            send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return;
        }

        if (!auth_can_manage_users(session.user_role)) {
            send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Permission denied: Administrator role required\"}");
            return;
        }

        char id_str[32];
        size_t cap_len = caps[0].len < sizeof(id_str) - 1 ? caps[0].len : sizeof(id_str) - 1;
        memcpy(id_str, caps[0].buf, cap_len);
        id_str[cap_len] = '\0';
        long long target_user_id = atoll(id_str);

        if (target_user_id <= 0) {
            send_json_reply(c, 400, active_origin, config, NULL, "{\"success\":false,\"error\":\"Invalid user ID\"}");
            return;
        }

        char *new_status = mg_json_get_str(hm->body, "$.status");
        char *new_role_str = mg_json_get_str(hm->body, "$.role");
        char err_msg[256] = {0};

        if (new_status) {
            int res = auth_update_user_status(config->db_client, session.user_id, session.user_role,
                                              session.organization_id, target_user_id,
                                              new_status, err_msg, sizeof(err_msg));
            free(new_status);
            if (res != 0) {
                free(new_role_str);
                int status_code = 400;
                char resp[512];
                if (strcmp(err_msg, "Target user not found") == 0) {
                    status_code = 404;
                    snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"User not found\"}");
                } else {
                    snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                }
                send_json_reply(c, status_code, active_origin, config, NULL, resp);
                return;
            }
        }

        if (new_role_str) {
            UserRole new_role = auth_role_from_string(new_role_str);
            int res = auth_update_user_role(config->db_client, session.user_id, session.user_role,
                                            session.organization_id, target_user_id,
                                            new_role, err_msg, sizeof(err_msg));
            free(new_role_str);
            if (res != 0) {
                int status_code = 400;
                char resp[512];
                if (strcmp(err_msg, "Target user not found") == 0) {
                    status_code = 404;
                    snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"User not found\"}");
                } else {
                    snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                }
                send_json_reply(c, status_code, active_origin, config, NULL, resp);
                return;
            }
        }

        send_json_reply(c, 200, active_origin, config, NULL, "{\"success\":true}");
        return;
    }

    send_json_reply(c, 404, active_origin, config, NULL, "{\"success\":false,\"error\":\"Endpoint Not Found\"}");
}

static void server_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        HttpServerConfig *cfg = (HttpServerConfig *)c->fn_data;
        http_handle_request(c, hm, cfg);
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        HttpServerConfig *cfg = (HttpServerConfig *)c->fn_data;
        ws_handle_message(c, wm, cfg ? cfg->db_client : NULL);
    } else if (ev == MG_EV_CLOSE) {
        ws_handle_close(c);
    }
}

int http_server_start(HttpServerConfig *config) {
    if (!config) {
        return -1;
    }

    if (strcmp(config->app_env, "production") == 0 || strstr(config->allowed_origins, "https://") != NULL) {
        config->cookie_secure = 1;
    }

    ws_init(config->db_client);

    mg_mgr_init(&g_http_mgr);
    char listen_addr[64];
    snprintf(listen_addr, sizeof(listen_addr), "http://0.0.0.0:%d", config->port);

    struct mg_connection *c = mg_http_listen(&g_http_mgr, listen_addr, server_fn, config);
    if (!c) {
        mg_mgr_free(&g_http_mgr);
        return -1;
    }

    if (config->instance_id[0] == '\0') {
        snprintf(config->instance_id, sizeof(config->instance_id), "inst_%d", config->port);
    }
    if (config->db_client) {
        if (outbox_consumer_init(&g_http_outbox_consumer, config->instance_id, config->db_client, NULL, NULL) == 0) {
            g_consumer_active = 1;
        }
    }

    g_server_running = 1;
    return 0;
}

void http_server_poll(int ms) {
    if (g_server_running) {
        mg_mgr_poll(&g_http_mgr, ms);
        if (g_consumer_active) {
            outbox_consumer_poll_all(&g_http_outbox_consumer, 50, NULL);
        }
    }
}

void http_server_stop(void) {
    if (g_server_running) {
        g_server_running = 0;
        if (g_consumer_active) {
            g_consumer_active = 0;
            outbox_consumer_destroy(&g_http_outbox_consumer);
        }
        ws_cleanup();
        mg_mgr_free(&g_http_mgr);
    }
}
