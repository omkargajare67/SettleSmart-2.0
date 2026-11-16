#include "transaction_routes.h"
#include "../transactions/transaction_service.h"
#include "../auth/authorization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int can_manage_transactions(UserRole role) {
    return (role == ROLE_ADMIN || role == ROLE_FINANCE_MANAGER);
}

static void transaction_to_json(const Transaction *t, char *out, size_t out_len) {
    if (!t || !out || out_len == 0) {
        return;
    }

    char rev_buf[32];
    if (t->reversal_of_id > 0) {
        snprintf(rev_buf, sizeof(rev_buf), "%lld", t->reversal_of_id);
    } else {
        snprintf(rev_buf, sizeof(rev_buf), "null");
    }

    char user_buf[32];
    if (t->created_by_user_id > 0) {
        snprintf(user_buf, sizeof(user_buf), "%lld", t->created_by_user_id);
    } else {
        snprintf(user_buf, sizeof(user_buf), "null");
    }

    snprintf(out, out_len,
             "{\"id\":%lld,\"organization_id\":%lld,\"sender_entity_id\":%lld,\"receiver_entity_id\":%lld,\"amount\":\"%s\",\"currency\":\"%s\",\"status\":\"%s\",\"type\":\"%s\",\"idempotency_key\":\"%s\",\"request_fingerprint\":\"%s\",\"reversal_of_id\":%s,\"created_by_user_id\":%s,\"created_at\":\"%s\"}",
             t->id, t->organization_id, t->sender_entity_id, t->receiver_entity_id,
             t->amount_str, t->currency,
             transaction_status_to_string(t->status),
             transaction_type_to_string(t->transaction_type),
             t->idempotency_key, t->request_fingerprint,
             rev_buf, user_buf, t->created_at);
}

static int extract_idempotency_key(struct mg_http_message *hm, char *out_key, size_t key_len, char *err_msg, size_t err_len) {
    struct mg_str *hdr = mg_http_get_header(hm, "Idempotency-Key");
    if (!hdr || hdr->len == 0) {
        snprintf(err_msg, err_len, "Missing required Idempotency-Key header");
        return 0;
    }

    if (hdr->len >= key_len) {
        snprintf(err_msg, err_len, "Idempotency-Key header too long");
        return 0;
    }

    memcpy(out_key, hdr->buf, hdr->len);
    out_key[hdr->len] = '\0';

    if (transaction_validate_idempotency_key(out_key) != 0) {
        snprintf(err_msg, err_len, "Invalid Idempotency-Key. Must be a valid UUID v4");
        return 0;
    }

    return 1;
}

int transaction_routes_handle(struct mg_connection *c, struct mg_http_message *hm,
                              const HttpServerConfig *config, const char *active_origin) {

    if (mg_strcmp(hm->method, mg_str("PATCH")) == 0 ||
        mg_strcmp(hm->method, mg_str("DELETE")) == 0 ||
        mg_strcmp(hm->method, mg_str("PUT")) == 0) {
        http_send_json_reply(c, 405, active_origin, config, NULL,
                             "{\"success\":false,\"error\":\"Transaction records are immutable and cannot be modified or deleted\"}");
        return 1;
    }

    if (mg_strcmp(hm->uri, mg_str("/api/transactions")) == 0 ||
        mg_strcmp(hm->uri, mg_str("/api/transactions/")) == 0) {

        if (mg_strcmp(hm->method, mg_str("POST")) == 0) {
            SessionInfo session;
            char raw_session[128];
            if (http_extract_authenticated_session(hm, config->db_client, &session,
                                                  raw_session, sizeof(raw_session)) != 0) {
                http_send_json_reply(c, 401, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
                return 1;
            }

            if (!can_manage_transactions(session.user_role)) {
                http_send_json_reply(c, 403, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Forbidden: Insufficient privileges to create transactions\"}");
                return 1;
            }

            if (!http_verify_csrf_header(hm, &session)) {
                http_send_json_reply(c, 403, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
                return 1;
            }

            char idem_key[65];
            char idem_err[256];
            if (!extract_idempotency_key(hm, idem_key, sizeof(idem_key), idem_err, sizeof(idem_err))) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", idem_err);
                http_send_json_reply(c, 400, active_origin, config, NULL, resp);
                return 1;
            }

            long long sender_id = 0;
            double d_sender = 0;
            if (mg_json_get_num(hm->body, "$.sender_entity_id", &d_sender)) {
                sender_id = (long long)d_sender;
            } else {
                char *s = mg_json_get_str(hm->body, "$.sender_entity_id");
                if (s) {
                    sender_id = atoll(s);
                    free(s);
                }
            }

            long long receiver_id = 0;
            double d_recv = 0;
            if (mg_json_get_num(hm->body, "$.receiver_entity_id", &d_recv)) {
                receiver_id = (long long)d_recv;
            } else {
                char *s = mg_json_get_str(hm->body, "$.receiver_entity_id");
                if (s) {
                    receiver_id = atoll(s);
                    free(s);
                }
            }

            char amount_buf[64] = {0};
            char *amount_str = mg_json_get_str(hm->body, "$.amount");
            if (amount_str) {
                strncpy(amount_buf, amount_str, sizeof(amount_buf) - 1);
                free(amount_str);
            } else {
                int toklen = 0;
                int off = mg_json_get(hm->body, "$.amount", &toklen);
                if (off >= 0 && toklen > 0 && (size_t)toklen < sizeof(amount_buf)) {
                    memcpy(amount_buf, hm->body.buf + off, (size_t)toklen);
                    amount_buf[toklen] = '\0';
                }
            }

            char currency[8] = "INR";
            char *curr_str = mg_json_get_str(hm->body, "$.currency");
            if (curr_str) {
                strncpy(currency, curr_str, sizeof(currency) - 1);
                free(curr_str);
            }

            if (sender_id <= 0 || receiver_id <= 0 || amount_buf[0] == '\0') {
                http_send_json_reply(c, 400, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Missing or invalid required fields: sender_entity_id, receiver_entity_id, amount\"}");
                return 1;
            }

            Transaction created_tx;
            int is_replay = 0;
            char err_msg[256] = {0};

            int res = transaction_service_create(config->db_client, session.organization_id, session.user_id,
                                                sender_id, receiver_id, amount_buf, currency,
                                                idem_key, &created_tx, &is_replay,
                                                err_msg, sizeof(err_msg));

            if (res == TRANSACTION_ERR_OK) {
                char tx_json[1024];
                transaction_to_json(&created_tx, tx_json, sizeof(tx_json));
                char resp[1280];
                int status_code = is_replay ? 200 : 201;
                snprintf(resp, sizeof(resp),
                         "{\"success\":true,\"idempotent_replay\":%s,\"transaction\":%s}",
                         is_replay ? "true" : "false", tx_json);
                http_send_json_reply(c, status_code, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == TRANSACTION_ERR_CONFLICT) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 409, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == TRANSACTION_ERR_INACTIVE_ENTITY || res == TRANSACTION_ERR_INVALID_PARAM) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 400, active_origin, config, NULL, resp);
                return 1;
            }

            if (res == TRANSACTION_ERR_NOT_FOUND) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 404, active_origin, config, NULL, resp);
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

            TransactionFilter filter;
            memset(&filter, 0, sizeof(filter));

            char var_buf[128];
            if (mg_http_get_var(&hm->query, "page", var_buf, sizeof(var_buf)) > 0) {
                filter.page = atoi(var_buf);
            }
            if (mg_http_get_var(&hm->query, "limit", var_buf, sizeof(var_buf)) > 0) {
                filter.limit = atoi(var_buf);
            }
            if (mg_http_get_var(&hm->query, "id", var_buf, sizeof(var_buf)) > 0) {
                filter.transaction_id = atoll(var_buf);
            }
            if (mg_http_get_var(&hm->query, "entity_id", var_buf, sizeof(var_buf)) > 0) {
                filter.entity_id = atoll(var_buf);
            }
            if (mg_http_get_var(&hm->query, "direction", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.direction, var_buf, sizeof(filter.direction) - 1);
            }
            if (mg_http_get_var(&hm->query, "from_date", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.from_date, var_buf, sizeof(filter.from_date) - 1);
            }
            if (mg_http_get_var(&hm->query, "to_date", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.to_date, var_buf, sizeof(filter.to_date) - 1);
            }
            if (mg_http_get_var(&hm->query, "min_amount", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.min_amount, var_buf, sizeof(filter.min_amount) - 1);
            }
            if (mg_http_get_var(&hm->query, "max_amount", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.max_amount, var_buf, sizeof(filter.max_amount) - 1);
            }
            if (mg_http_get_var(&hm->query, "currency", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.currency, var_buf, sizeof(filter.currency) - 1);
            }
            if (mg_http_get_var(&hm->query, "status", var_buf, sizeof(var_buf)) > 0) {
                filter.status_filter = transaction_status_from_string(var_buf);
            }
            if (mg_http_get_var(&hm->query, "sort_by", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.sort_by, var_buf, sizeof(filter.sort_by) - 1);
            }
            if (mg_http_get_var(&hm->query, "order", var_buf, sizeof(var_buf)) > 0) {
                strncpy(filter.order, var_buf, sizeof(filter.order) - 1);
            }

            Transaction txs[100];
            TransactionPagination pagination;
            char err_msg[256] = {0};

            int count = transaction_service_list(config->db_client, session.organization_id, &filter,
                                                txs, 100, &pagination, err_msg, sizeof(err_msg));
            if (count < 0) {
                char resp[512];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
                http_send_json_reply(c, 500, active_origin, config, NULL, resp);
                return 1;
            }

            size_t buf_size = 131072;
            char *resp = (char *)malloc(buf_size);
            if (!resp) {
                http_send_json_reply(c, 500, active_origin, config, NULL,
                                     "{\"success\":false,\"error\":\"Out of memory\"}");
                return 1;
            }

            size_t pos = 0;
            pos += (size_t)snprintf(resp + pos, buf_size - pos, "{\"success\":true,\"data\":[");

            for (int i = 0; i < count; i++) {
                char tj[1024];
                transaction_to_json(&txs[i], tj, sizeof(tj));
                if (i > 0) {
                    pos += (size_t)snprintf(resp + pos, buf_size - pos, ",");
                }
                pos += (size_t)snprintf(resp + pos, buf_size - pos, "%s", tj);
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

    if (mg_strcmp(hm->uri, mg_str("/api/transactions/recent")) == 0 ||
        mg_strcmp(hm->uri, mg_str("/api/transactions/recent/")) == 0) {

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

        int limit = 20;
        char var_buf[32];
        if (mg_http_get_var(&hm->query, "limit", var_buf, sizeof(var_buf)) > 0) {
            limit = atoi(var_buf);
        }

        Transaction txs[100];
        int count = 0;
        char err_msg[256] = {0};

        int res = transaction_service_get_recent(config->db_client, session.organization_id, limit,
                                                 txs, 100, &count, err_msg, sizeof(err_msg));
        if (res != TRANSACTION_ERR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        size_t buf_size = 131072;
        char *resp = (char *)malloc(buf_size);
        if (!resp) {
            http_send_json_reply(c, 500, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Out of memory\"}");
            return 1;
        }

        size_t pos = 0;
        pos += (size_t)snprintf(resp + pos, buf_size - pos, "{\"success\":true,\"data\":[");

        for (int i = 0; i < count; i++) {
            char tj[1024];
            transaction_to_json(&txs[i], tj, sizeof(tj));
            if (i > 0) {
                pos += (size_t)snprintf(resp + pos, buf_size - pos, ",");
            }
            pos += (size_t)snprintf(resp + pos, buf_size - pos, "%s", tj);
        }

        pos += (size_t)snprintf(resp + pos, buf_size - pos, "],\"count\":%d}", count);

        http_send_json_reply(c, 200, active_origin, config, NULL, resp);
        free(resp);
        return 1;
    }

    struct mg_str caps[2];
    if (mg_match(hm->uri, mg_str("/api/transactions/*/reversal"), caps)) {
        char id_str[32];
        size_t cap_len = caps[0].len < sizeof(id_str) - 1 ? caps[0].len : sizeof(id_str) - 1;
        memcpy(id_str, caps[0].buf, cap_len);
        id_str[cap_len] = '\0';
        long long orig_id = atoll(id_str);

        if (orig_id <= 0) {
            http_send_json_reply(c, 400, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Invalid transaction ID\"}");
            return 1;
        }

        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
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

        if (!can_manage_transactions(session.user_role)) {
            http_send_json_reply(c, 403, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Forbidden: Insufficient privileges to reverse transactions\"}");
            return 1;
        }

        if (!http_verify_csrf_header(hm, &session)) {
            http_send_json_reply(c, 403, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
            return 1;
        }

        char idem_key[65];
        char idem_err[256];
        if (!extract_idempotency_key(hm, idem_key, sizeof(idem_key), idem_err, sizeof(idem_err))) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", idem_err);
            http_send_json_reply(c, 400, active_origin, config, NULL, resp);
            return 1;
        }

        Transaction rev_tx;
        int is_replay = 0;
        char err_msg[256] = {0};

        int res = transaction_service_create_reversal(config->db_client, session.organization_id, session.user_id,
                                                      orig_id, idem_key, &rev_tx, &is_replay,
                                                      err_msg, sizeof(err_msg));

        if (res == TRANSACTION_ERR_OK) {
            char tx_json[1024];
            transaction_to_json(&rev_tx, tx_json, sizeof(tx_json));
            char resp[1280];
            int status_code = is_replay ? 200 : 201;
            snprintf(resp, sizeof(resp),
                     "{\"success\":true,\"idempotent_replay\":%s,\"transaction\":%s}",
                     is_replay ? "true" : "false", tx_json);
            http_send_json_reply(c, status_code, active_origin, config, NULL, resp);
            return 1;
        }

        if (res == TRANSACTION_ERR_CONFLICT) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 409, active_origin, config, NULL, resp);
            return 1;
        }

        if (res == TRANSACTION_ERR_NOT_FOUND) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 404, active_origin, config, NULL, resp);
            return 1;
        }

        if (res == TRANSACTION_ERR_INVALID_PARAM || res == TRANSACTION_ERR_INACTIVE_ENTITY) {
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

    if (mg_match(hm->uri, mg_str("/api/transactions/*"), caps)) {
        char id_str[32];
        size_t cap_len = caps[0].len < sizeof(id_str) - 1 ? caps[0].len : sizeof(id_str) - 1;
        memcpy(id_str, caps[0].buf, cap_len);
        id_str[cap_len] = '\0';

        if (strcmp(id_str, "recent") == 0) {
            return 0;
        }

        long long tx_id = atoll(id_str);
        if (tx_id <= 0) {
            http_send_json_reply(c, 400, active_origin, config, NULL,
                                 "{\"success\":false,\"error\":\"Invalid transaction ID\"}");
            return 1;
        }

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

        Transaction tx;
        char err_msg[256] = {0};

        int res = transaction_service_get_by_id(config->db_client, session.organization_id, tx_id,
                                                &tx, err_msg, sizeof(err_msg));
        if (res == TRANSACTION_ERR_OK) {
            char tx_json[1024];
            transaction_to_json(&tx, tx_json, sizeof(tx_json));
            char resp[1280];
            snprintf(resp, sizeof(resp), "{\"success\":true,\"transaction\":%s}", tx_json);
            http_send_json_reply(c, 200, active_origin, config, NULL, resp);
            return 1;
        }

        if (res == TRANSACTION_ERR_NOT_FOUND) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
            http_send_json_reply(c, 404, active_origin, config, NULL, resp);
            return 1;
        }

        char resp[512];
        snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg);
        http_send_json_reply(c, 500, active_origin, config, NULL, resp);
        return 1;
    }

    return 0;
}
