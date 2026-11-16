#include "websocket.h"
#include "event_dispatcher.h"
#include "../auth/session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ws_is_origin_allowed(const char *allowed_origins, const char *origin) {
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
        if (strcmp(token, origin) == 0) {
            return 1;
        }
        token = strtok(NULL, ",");
    }
    return 0;
}

static int ws_parse_cookie(const char *cookie_str, size_t cookie_len,
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

static WsClient g_ws_clients[WS_MAX_CLIENTS];
static DBClient *g_ws_db_client = NULL;

void ws_init(DBClient *db_client) {
    g_ws_db_client = db_client;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        g_ws_clients[i].conn = NULL;
        g_ws_clients[i].organization_id = 0;
        g_ws_clients[i].user_id = 0;
        g_ws_clients[i].is_active = 0;
    }
}

int ws_handle_upgrade(struct mg_connection *c, struct mg_http_message *hm,
                      const char *allowed_origins, DBClient *db_client) {
    if (!c || !hm || !db_client) {
        return -1;
    }

    struct mg_str *origin_hdr = mg_http_get_header(hm, "Origin");
    if (origin_hdr && origin_hdr->len > 0) {
        char origin[256];
        size_t o_len = origin_hdr->len < sizeof(origin) - 1 ? origin_hdr->len : sizeof(origin) - 1;
        memcpy(origin, origin_hdr->buf, o_len);
        origin[o_len] = '\0';

        if (!ws_is_origin_allowed(allowed_origins, origin)) {
            mg_http_reply(c, 403, "Content-Type: application/json\r\n", "{\"success\":false,\"error\":\"Origin not allowed\"}");
            return -1;
        }
    }

    struct mg_str *cookie_hdr = mg_http_get_header(hm, "Cookie");
    if (!cookie_hdr || cookie_hdr->len == 0) {
        mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"success\":false,\"error\":\"Unauthorized\"}");
        return -1;
    }

    char raw_session[128];
    if (ws_parse_cookie(cookie_hdr->buf, cookie_hdr->len, "session_token", raw_session, sizeof(raw_session)) != 0) {
        mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"success\":false,\"error\":\"Unauthorized\"}");
        return -1;
    }

    SessionInfo session;
    if (session_validate(db_client, raw_session, &session) != 0) {
        mg_http_reply(c, 401, "Content-Type: application/json\r\n", "{\"success\":false,\"error\":\"Unauthorized\"}");
        return -1;
    }

    mg_ws_upgrade(c, hm, NULL);

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!g_ws_clients[i].is_active || g_ws_clients[i].conn == c) {
            g_ws_clients[i].conn = c;
            g_ws_clients[i].organization_id = session.organization_id;
            g_ws_clients[i].user_id = session.user_id;
            g_ws_clients[i].is_active = 1;
            break;
        }
    }

    return 0;
}

int ws_replay_events(struct mg_connection *c, DBClient *db_client, long long org_id, int64_t last_seen_seq) {
    if (!c || !db_client || !db_client->connection || org_id <= 0) {
        return -1;
    }

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT event_sequence, event_type, payload FROM event_outbox "
             "WHERE organization_id = %lld AND event_sequence > %lld "
             "ORDER BY event_sequence ASC LIMIT 200;",
             org_id, (long long)last_seen_seq);

    if (db_client_query(db_client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(db_client);
    if (!res) {
        return -1;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        int64_t seq = atoll(row[0]);
        const char *ev_type = row[1] ? row[1] : "TRANSACTION_CREATED";
        const char *payload = row[2] ? row[2] : "{}";

        char ws_msg[4096];
        if (event_dispatcher_format_ws_message(ev_type, seq, payload, ws_msg, sizeof(ws_msg)) == 0) {
            mg_ws_send(c, ws_msg, strlen(ws_msg), WEBSOCKET_OP_TEXT);
        }
    }
    db_client_free_result(res);
    return 0;
}

void ws_handle_message(struct mg_connection *c, struct mg_ws_message *wm, DBClient *db_client) {
    if (!c || !wm) {
        return;
    }

    WsClient *client = NULL;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (g_ws_clients[i].is_active && g_ws_clients[i].conn == c) {
            client = &g_ws_clients[i];
            break;
        }
    }

    if (!client) {
        return;
    }

    char *msg_type = mg_json_get_str(wm->data, "$.type");
    if (!msg_type) {
        return;
    }

    if (strcmp(msg_type, "PING") == 0) {
        free(msg_type);
        mg_ws_send(c, "{\"type\":\"PONG\"}", 15, WEBSOCKET_OP_TEXT);
        return;
    }

    if (strcmp(msg_type, "SUBSCRIBE_GRAPH") == 0) {
        free(msg_type);
        double d_seq = 0;
        int64_t last_seq = 0;
        if (mg_json_get_num(wm->data, "$.last_seen_sequence", &d_seq)) {
            last_seq = (int64_t)d_seq;
        }
        DBClient *active_db = db_client ? db_client : g_ws_db_client;
        if (active_db) {
            ws_replay_events(c, active_db, client->organization_id, last_seq);
        }
        return;
    }

    free(msg_type);
}

void ws_handle_close(struct mg_connection *c) {
    if (!c) {
        return;
    }

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (g_ws_clients[i].conn == c) {
            g_ws_clients[i].is_active = 0;
            g_ws_clients[i].conn = NULL;
            g_ws_clients[i].organization_id = 0;
            g_ws_clients[i].user_id = 0;
            break;
        }
    }
}

void ws_broadcast_event(long long org_id, const char *msg) {
    if (org_id <= 0 || !msg) {
        return;
    }

    size_t len = strlen(msg);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (g_ws_clients[i].is_active && g_ws_clients[i].conn && g_ws_clients[i].organization_id == org_id) {
            mg_ws_send(g_ws_clients[i].conn, msg, len, WEBSOCKET_OP_TEXT);
        }
    }
}

void ws_cleanup(void) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        g_ws_clients[i].conn = NULL;
        g_ws_clients[i].organization_id = 0;
        g_ws_clients[i].user_id = 0;
        g_ws_clients[i].is_active = 0;
    }
    g_ws_db_client = NULL;
}
