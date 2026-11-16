#include "entity_routes.h"
#include "../entities/entity_service.h"
#include "../auth/authorization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int can_manage_entities(UserRole role) {
    return (role == ROLE_ADMIN || role == ROLE_FINANCE_MANAGER);
}

static void entity_to_json(const Entity *e, char *out, size_t out_len) {
    if (!e || !out || out_len == 0) {
        return;
    }
    snprintf(out, out_len,
             "{\"id\":%lld,\"organization_id\":%lld,\"name\":\"%s\",\"entity_type\":\"%s\",\"status\":\"%s\",\"created_by_user_id\":%lld,\"updated_by_user_id\":%lld,\"created_at\":\"%s\",\"updated_at\":\"%s\"}",
             e->id, e->organization_id, e->name,
             entity_type_to_string(e->entity_type),
             entity_status_to_string(e->status),
             e->created_by_user_id, e->updated_by_user_id,
             e->created_at, e->updated_at);
}

int entity_routes_handle(struct mg_connection *c, struct mg_http_message *hm,
                         const HttpServerConfig *config, const char *active_origin) {
    if (mg_strcmp(hm->uri, mg_str("/api/entities")) == 0 ||
        mg_strcmp(hm->uri, mg_str("/api/entities/")) == 0) {

        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            SessionInfo session;
            char raw_session[128];
            if (http_extract_authenticated_session(hm, config->db_client, &session,
                                                  raw_session, sizeof(raw_session)) != 0) {
                http_send_json_reply(c, 401, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
                return 1;
            }

            if (!can_manage_entities(session.user_role)) {
                http_send_json_reply(c, 403, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Forbidden: Insufficient privileges to manage entities\"}");
                return 1;
            }

            if (!http_verify_csrf_header(hm, &session)) {
                http_send_json_reply(c, 403, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
                return 1;
            }

            char *name = mg_json_get_str(hm->body, "$.name");
            char *type_str = mg_json_get_str(hm->body, "$.entity_type");

            if (!name || !type_str) {
                free(name);
                free(type_str);
                http_send_json_reply(c, 400, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Missing required fields: name and entity_type\"}");
                return 1;
            }

            EntityType etype = entity_type_from_string(type_str);
            Entity created;
            char err_msg[256] = {0};

            int res = entity_service_create(config->db_client, session.organization_id, session.user_id,
                                           name, etype, &created, err_msg, sizeof(err_msg));
            free(name);
            free(type_str);

            if (res == ENTITY_ERR_OK) {
                char entity_json[512];
                entity_to_json(&created, entity_json, sizeof(entity_json));
                char resp[768];
                snprintf(resp, sizeof(resp), "{\"success\":true,\"entity\":%s}", entity_json);
                http_send_json_reply(c, 201, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == ENTITY_ERR_CONFLICT) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 409, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == ENTITY_ERR_INVALID_PARAM) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 400, active_origin, config, NULL, resp);
                return 1;
            }

            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            SessionInfo session;
            char raw_session[128];
            if (http_extract_authenticated_session(hm, config->db_client, &session,
                                                  raw_session, sizeof(raw_session)) != 0) {
                http_send_json_reply(c, 401, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
                return 1;
            }

            EntityFilter filter;
            memset(&filter, 0, sizeof(filter));

            char var_buf[128];
            if (mg_http_get_var(&hm->query, "page", var_buf, sizeof(var_buf)) > 0) {
                filter.page = atoi(var_buf);
            }
            if (mg_http_get_var(&hm->query, "limit", var_buf, sizeof(var_buf)) > 0) {
                filter.limit = atoi(var_buf);
            }
            if (mg_http_get_var(&hm->query, "search", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.search, var_buf, sizeof(filter.search) - 1);
            }
            if (mg_http_get_var(&hm->query, "type", var_buf, sizeof(var_buf)) > 0) {
                filter.type_filter = entity_type_from_string(var_buf);
            } else if (mg_http_get_var(&hm->query, "entity_type", var_buf, sizeof(var_buf)) > 0) {
                filter.type_filter = entity_type_from_string(var_buf);
            }
            if (mg_http_get_var(&hm->query, "status", var_buf, sizeof(var_buf)) > 0) {
                filter.status_filter = entity_status_from_string(var_buf);
            }
            if (mg_http_get_var(&hm->query, "sort_by", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.sort_by, var_buf, sizeof(filter.sort_by) - 1);
            }
            if (mg_http_get_var(&hm->query, "order", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.order, var_buf, sizeof(filter.order) - 1);
            }

            Entity entities[100];
            EntityPagination pagination;
            char err_msg[256] = {0};

            int count = entity_service_list(config->db_client, session.organization_id, &filter,
                                           entities, 100, &pagination, err_msg, sizeof(err_msg));
            if (count < 0) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 500, active_origin, config, NULL, resp);
                return 1;
            }

            size_t buf_size = 65536;
            char *resp = (char *)malloc(buf_size);
            if (!resp) {
                http_send_json_reply(c, 500, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Out of memory\"}");
                return 1;
            }

            size_t pos = 0;
            pos += (size_t)snprintf(resp + pos, buf_size - pos, "{\"success\":true,\"data\":[");

            for (int i = 0; i < count; i++) {
                char ej[512];
                entity_to_json(&entities[i], ej, sizeof(ej));
                if (i > 0) {
                    pos += (size_t)snprintf(resp + pos, buf_size - pos, ",");
                }
                pos += (size_t)snprintf(resp + pos, buf_size - pos, "%s", ej);
            }

            pos += (size_t)snprintf(resp + pos, buf_size - pos,
                                   "],\"pagination\":{\"page\":%d,\"limit\":%d,\"total_records\":%d,\"total_pages\":%d}}",
                                   pagination.page, pagination.limit, pagination.total_records, pagination.total_pages);

            http_send_json_reply(c, 200, active_origin, config, NULL, resp);
            free(resp);
            return 1;
        }

        http_send_json_reply(c, 405, active_origin, config, NULL,
                             "{\"success\":false,\"error\":\"Method Not Allowed\"}");
        return 1;
    }

    struct mg_str caps[2];
    if (mg_match(hm->uri, mg_str("/api/entities/*/status"), caps)) {
        char id_str[32];
        size_t cap_len = caps[0].len < sizeof(id_str) - 1 ? caps[0].len : sizeof(id_str) - 1;
        memcpy(id_str, caps[0].buf, cap_len);
        id_str[cap_len] = '\0';
        long long target_id = atoll(id_str);

        if (target_id <= 0) {
            http_send_json_reply(c, 400, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Invalid entity ID\"}");
            return 1;
        }

        if (mg_strcmp(hm->method, mg_str("PATCH")) != 0) {
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

        if (!can_manage_entities(session.user_role)) {
            http_send_json_reply(c, 403, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Forbidden: Insufficient privileges to manage entities\"}");
            return 1;
        }

        if (!http_verify_csrf_header(hm, &session)) {
            http_send_json_reply(c, 403, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
            return 1;
        }

        char *status_str = mg_json_get_str(hm->body, "$.status");
        if (!status_str) {
            http_send_json_reply(c, 400, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Missing required field: status\"}");
            return 1;
        }

        EntityStatus new_status = entity_status_from_string(status_str);
        free(status_str);

        Entity updated;
        char err_msg[256] = {0};

        int res = entity_service_update_status(config->db_client, session.organization_id, session.user_id,
                                              target_id, new_status, &updated, err_msg, sizeof(err_msg));
        if (res == ENTITY_ERR_OK) {
            char ej[512];
            entity_to_json(&updated, ej, sizeof(ej));
            char resp[768];
            snprintf(resp, sizeof(resp), "{\"success\":true,\"entity\":%s}", ej);
            http_send_json_reply(c, 200, active_origin, config, NULL, resp);
            return 1;
        }

        if (res == ENTITY_ERR_NOT_FOUND) {
            http_send_json_reply(c, 404, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Entity not found\"}");
            return 1;
        }

        if (res == ENTITY_ERR_INVALID_PARAM) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 400, active_origin, config, NULL, resp);
            return 1;
        }

        char resp[512];
        snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
        http_send_json_reply(c, 500, active_origin, config, NULL, resp);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/entities/*"), caps)) {
        char id_str[32];
        size_t cap_len = caps[0].len < sizeof(id_str) - 1 ? caps[0].len : sizeof(id_str) - 1;
        memcpy(id_str, caps[0].buf, cap_len);
        id_str[cap_len] = '\0';
        long long target_id = atoll(id_str);

        if (target_id <= 0) {
            http_send_json_reply(c, 400, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Invalid entity ID\"}");
            return 1;
        }

        if (mg_strcmp(hm->method, mg_str("DELETE")) == 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Physical deletion of entities is not permitted\"}");
            return 1;
        }

        if (mg_strcmp(hm->method, mg_str("GET")) == 0) {
            SessionInfo session;
            char raw_session[128];
            if (http_extract_authenticated_session(hm, config->db_client, &session,
                                                  raw_session, sizeof(raw_session)) != 0) {
                http_send_json_reply(c, 401, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
                return 1;
            }

            Entity entity;
            char err_msg[256] = {0};
            int res = entity_service_get_by_id(config->db_client, session.organization_id,
                                              target_id, &entity, err_msg, sizeof(err_msg));
            if (res == ENTITY_ERR_OK) {
                char ej[512];
                entity_to_json(&entity, ej, sizeof(ej));
                char resp[768];
                snprintf(resp, sizeof(resp), "{\"success\":true,\"entity\":%s}", ej);
                http_send_json_reply(c, 200, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == ENTITY_ERR_NOT_FOUND) {
                http_send_json_reply(c, 404, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Entity not found\"}");
                return 1;
            }

            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        if (mg_strcmp(hm->method, mg_str("PATCH")) == 0) {
            SessionInfo session;
            char raw_session[128];
            if (http_extract_authenticated_session(hm, config->db_client, &session,
                                                  raw_session, sizeof(raw_session)) != 0) {
                http_send_json_reply(c, 401, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
                return 1;
            }

            if (!can_manage_entities(session.user_role)) {
                http_send_json_reply(c, 403, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Forbidden: Insufficient privileges to manage entities\"}");
                return 1;
            }

            if (!http_verify_csrf_header(hm, &session)) {
                http_send_json_reply(c, 403, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
                return 1;
            }

            char *name = mg_json_get_str(hm->body, "$.name");
            char *type_str = mg_json_get_str(hm->body, "$.entity_type");
            EntityType etype = type_str ? entity_type_from_string(type_str) : ENTITY_TYPE_UNKNOWN;

            Entity updated;
            char err_msg[256] = {0};

            int res = entity_service_update(config->db_client, session.organization_id, session.user_id,
                                           target_id, name, etype, &updated, err_msg, sizeof(err_msg));
            free(name);
            free(type_str);

            if (res == ENTITY_ERR_OK) {
                char ej[512];
                entity_to_json(&updated, ej, sizeof(ej));
                char resp[768];
                snprintf(resp, sizeof(resp), "{\"success\":true,\"entity\":%s}", ej);
                http_send_json_reply(c, 200, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == ENTITY_ERR_NOT_FOUND) {
                http_send_json_reply(c, 404, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Entity not found\"}");
                return 1;
            }

            if (res == ENTITY_ERR_CONFLICT) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 409, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == ENTITY_ERR_INVALID_PARAM) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 400, active_origin, config, NULL, resp);
                return 1;
            }

            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        http_send_json_reply(c, 405, active_origin, config, NULL,
                             "{\"success\":false,\"error\":\"Method Not Allowed\"}");
        return 1;
    }

    return 0;
}
