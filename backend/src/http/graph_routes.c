#include "graph_routes.h"
#include "../graph/graph_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int graph_routes_handle(struct mg_connection *c, struct mg_http_message *hm,
                        const HttpServerConfig *config, const char *active_origin) {
    if (!c || !hm || !config) {
        return 0;
    }

    if (mg_match(hm->uri, mg_str("/api/graph"), NULL) ||
        mg_match(hm->uri, mg_str("/api/graph/"), NULL)) {

        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session,
                                              raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        int page = 1;
        int limit = 100;
        long long entity_id = 0;
        char status[64] = {0};
        char from_date[64] = {0};
        char to_date[64] = {0};

        char var_buf[128];
        if (mg_http_get_var(&hm->query, "page", var_buf, sizeof(var_buf)) > 0) {
            page = atoi(var_buf);
        }
        if (mg_http_get_var(&hm->query, "limit", var_buf, sizeof(var_buf)) > 0) {
            limit = atoi(var_buf);
        }
        if (mg_http_get_var(&hm->query, "entity_id", var_buf, sizeof(var_buf)) > 0) {
            entity_id = atoll(var_buf);
        }
        if (mg_http_get_var(&hm->query, "status", var_buf, sizeof(var_buf)) > 0) {
            strncpy(status, var_buf, sizeof(status) - 1);
        }
        if (mg_http_get_var(&hm->query, "from_date", var_buf, sizeof(var_buf)) > 0) {
            strncpy(from_date, var_buf, sizeof(from_date) - 1);
        }
        if (mg_http_get_var(&hm->query, "to_date", var_buf, sizeof(var_buf)) > 0) {
            strncpy(to_date, var_buf, sizeof(to_date) - 1);
        }

        size_t json_cap = 262144;
        char *snapshot_json = (char *)malloc(json_cap);
        if (!snapshot_json) {
            http_send_json_reply(c, 500, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Internal Server Error\"}");
            return 1;
        }

        if (graph_service_get_snapshot_json(config->db_client, session.organization_id,
                                            entity_id, status, from_date, to_date,
                                            page, limit, snapshot_json, json_cap) != 0) {
            free(snapshot_json);
            http_send_json_reply(c, 500, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Failed to retrieve graph snapshot\"}");
            return 1;
        }

        http_send_json_reply(c, 200, active_origin, config, NULL, snapshot_json);
        free(snapshot_json);
        return 1;
    }

    return 0;
}
