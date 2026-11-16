#include "processing_routes.h"
#include "../transactions/transaction_processor.h"
#include "../auth/authorization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int can_process_transactions(UserRole role) {
    return (role == ROLE_ADMIN || role == ROLE_FINANCE_MANAGER);
}

static void transaction_to_json_str(const Transaction *t, char *out, size_t out_len) {
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

static void format_paise_str(int64_t paise, char *out, size_t out_len) {
    int is_neg = paise < 0;
    int64_t abs_val = is_neg ? -paise : paise;
    int64_t rupees = abs_val / 100;
    int paise_rem = (int)(abs_val % 100);
    if (is_neg) {
        snprintf(out, out_len, "-%lld.%02d", (long long)rupees, paise_rem);
    } else {
        snprintf(out, out_len, "%lld.%02d", (long long)rupees, paise_rem);
    }
}

int processing_routes_handle(struct mg_connection *c, struct mg_http_message *hm,
                             const HttpServerConfig *config, const char *active_origin) {
    if (!c || !hm || !config) {
        return 0;
    }

    if (mg_match(hm->uri, mg_str("/api/transactions/process-next"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        if (!can_process_transactions(session.user_role)) {
            http_send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Forbidden: Insufficient privileges to process transactions\"}");
            return 1;
        }

        if (!http_verify_csrf_header(hm, &session)) {
            http_send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
            return 1;
        }

        Transaction tx;
        char err_msg[256];
        int rc = transaction_processor_process_next(config->db_client, NULL, NULL, session.organization_id, &tx, err_msg, sizeof(err_msg));
        if (rc == TRANSACTION_PROCESSOR_NO_PENDING) {
            http_send_json_reply(c, 404, active_origin, config, NULL, "{\"success\":false,\"error\":\"No pending transactions to process\"}");
            return 1;
        }

        if (rc != TRANSACTION_PROCESSOR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg[0] ? err_msg : "Processing failed");
            http_send_json_reply(c, 400, active_origin, config, NULL, resp);
            return 1;
        }

        char tx_json[1024];
        transaction_to_json_str(&tx, tx_json, sizeof(tx_json));
        char resp[1536];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"message\":\"Transaction processed successfully\",\"transaction\":%s}", tx_json);
        http_send_json_reply(c, 200, active_origin, config, NULL, resp);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/transactions/process-batch"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("POST")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        if (!can_process_transactions(session.user_role)) {
            http_send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Forbidden: Insufficient privileges to process transactions\"}");
            return 1;
        }

        if (!http_verify_csrf_header(hm, &session)) {
            http_send_json_reply(c, 403, active_origin, config, NULL, "{\"success\":false,\"error\":\"Invalid or missing CSRF token\"}");
            return 1;
        }

        int batch_size = 10;
        double d_batch = 0;
        if (mg_json_get_num(hm->body, "$.batch_size", &d_batch)) {
            batch_size = (int)d_batch;
        } else {
            char var_buf[32];
            if (mg_http_get_var(&hm->query, "batch_size", var_buf, sizeof(var_buf)) > 0) {
                batch_size = atoi(var_buf);
            }
        }

        int processed_count = 0;
        char err_msg[256];
        int rc = transaction_processor_process_batch(config->db_client, NULL, NULL, session.organization_id, batch_size, &processed_count, err_msg, sizeof(err_msg));
        if (rc != TRANSACTION_PROCESSOR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\",\"processed_count\":%d}", err_msg[0] ? err_msg : "Batch processing error", processed_count);
            http_send_json_reply(c, 400, active_origin, config, NULL, resp);
            return 1;
        }

        char resp[512];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"message\":\"Batch processing completed\",\"processed_count\":%d}", processed_count);
        http_send_json_reply(c, 200, active_origin, config, NULL, resp);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/transactions/processing/status"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        ProcessingStatus status;
        char err_msg[256];
        if (transaction_processor_get_status(config->db_client, session.organization_id, &status, err_msg, sizeof(err_msg)) != TRANSACTION_PROCESSOR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg[0] ? err_msg : "Failed to get processing status");
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"status\":{\"organization_id\":%lld,\"pending_count\":%d,\"completed_count\":%d,\"reversed_count\":%d,\"disputed_count\":%d,\"total_count\":%d}}",
                 status.organization_id, status.pending_count, status.completed_count, status.reversed_count, status.disputed_count, status.total_count);
        http_send_json_reply(c, 200, active_origin, config, NULL, resp);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/transactions/pending/priority"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        int limit = 50;
        char var_buf[32];
        if (mg_http_get_var(&hm->query, "limit", var_buf, sizeof(var_buf)) > 0) {
            limit = atoi(var_buf);
        }

        Transaction tx_arr[200];
        int count = 0;
        char err_msg[256];
        if (transaction_processor_get_pending_priority(config->db_client, session.organization_id, limit, tx_arr, 200, &count, err_msg, sizeof(err_msg)) != TRANSACTION_PROCESSOR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg[0] ? err_msg : "Failed to fetch priority pending transactions");
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        size_t json_cap = 262144;
        char *resp_json = (char *)malloc(json_cap);
        if (!resp_json) {
            http_send_json_reply(c, 500, active_origin, config, NULL, "{\"success\":false,\"error\":\"Out of memory\"}");
            return 1;
        }

        size_t pos = snprintf(resp_json, json_cap, "{\"success\":true,\"count\":%d,\"transactions\":[", count);
        for (int i = 0; i < count; i++) {
            char item_buf[1024];
            transaction_to_json_str(&tx_arr[i], item_buf, sizeof(item_buf));
            pos += snprintf(resp_json + pos, json_cap - pos, "%s%s", (i > 0 ? "," : ""), item_buf);
        }
        snprintf(resp_json + pos, json_cap - pos, "]}");

        http_send_json_reply(c, 200, active_origin, config, NULL, resp_json);
        free(resp_json);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/entities/balances"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        EntityBalance *balances = NULL;
        size_t count = 0;
        char err_msg[256];
        if (transaction_processor_get_entity_balances(config->db_client, session.organization_id, &balances, &count, err_msg, sizeof(err_msg)) != TRANSACTION_PROCESSOR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg[0] ? err_msg : "Failed to calculate balances");
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        size_t json_cap = 262144;
        char *resp_json = (char *)malloc(json_cap);
        if (!resp_json) {
            transaction_processor_free_entity_balances(balances);
            http_send_json_reply(c, 500, active_origin, config, NULL, "{\"success\":false,\"error\":\"Out of memory\"}");
            return 1;
        }

        size_t pos = snprintf(resp_json, json_cap, "{\"success\":true,\"count\":%zu,\"balances\":[", count);
        for (size_t i = 0; i < count; i++) {
            char bal_str[32];
            format_paise_str(balances[i].balance_paise, bal_str, sizeof(bal_str));
            pos += snprintf(resp_json + pos, json_cap - pos, "%s{\"entity_id\":%lld,\"balance_paise\":%lld,\"balance_str\":\"%s\"}",
                            (i > 0 ? "," : ""), balances[i].entity_id, (long long)balances[i].balance_paise, bal_str);
        }
        snprintf(resp_json + pos, json_cap - pos, "]}");

        transaction_processor_free_entity_balances(balances);
        http_send_json_reply(c, 200, active_origin, config, NULL, resp_json);
        free(resp_json);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/settlements/recommendations"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        SettlementPlan plan;
        settlement_plan_init(&plan, 16);
        char err_msg[256];

        int rc = transaction_processor_get_settlement_recommendations(config->db_client, session.organization_id, &plan, err_msg, sizeof(err_msg));
        if (rc != TRANSACTION_PROCESSOR_OK) {
            settlement_plan_destroy(&plan);
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg[0] ? err_msg : "Failed to calculate settlement recommendations");
            http_send_json_reply(c, 400, active_origin, config, NULL, resp);
            return 1;
        }

        size_t json_cap = 262144;
        char *resp_json = (char *)malloc(json_cap);
        if (!resp_json) {
            settlement_plan_destroy(&plan);
            http_send_json_reply(c, 500, active_origin, config, NULL, "{\"success\":false,\"error\":\"Out of memory\"}");
            return 1;
        }

        char total_str[32];
        format_paise_str(plan.total_settled_paise, total_str, sizeof(total_str));

        size_t pos = snprintf(resp_json, json_cap,
                              "{\"success\":true,\"total_settled_paise\":%lld,\"total_settled_str\":\"%s\",\"recommendation_count\":%zu,\"recommendations\":[",
                              (long long)plan.total_settled_paise, total_str, plan.count);

        for (size_t i = 0; i < plan.count; i++) {
            pos += snprintf(resp_json + pos, json_cap - pos,
                            "%s{\"from_entity_id\":%lld,\"to_entity_id\":%lld,\"amount_paise\":%lld,\"amount_str\":\"%s\"}",
                            (i > 0 ? "," : ""),
                            plan.recommendations[i].from_entity_id,
                            plan.recommendations[i].to_entity_id,
                            (long long)plan.recommendations[i].amount_paise,
                            plan.recommendations[i].amount_str);
        }
        snprintf(resp_json + pos, json_cap - pos, "]}");

        settlement_plan_destroy(&plan);
        http_send_json_reply(c, 200, active_origin, config, NULL, resp_json);
        free(resp_json);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/transactions/recent/sorted"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        SortCriteria criteria = SORT_BY_TIMESTAMP;
        SortOrder order = SORT_ORDER_DESC;
        int limit = 50;

        char var_buf[32];
        if (mg_http_get_var(&hm->query, "sort_by", var_buf, sizeof(var_buf)) > 0) {
            if (strcmp(var_buf, "amount") == 0) {
                criteria = SORT_BY_AMOUNT;
            } else if (strcmp(var_buf, "id") == 0) {
                criteria = SORT_BY_ID;
            } else if (strcmp(var_buf, "timestamp") == 0 || strcmp(var_buf, "created_at") == 0) {
                criteria = SORT_BY_TIMESTAMP;
            }
        }

        if (mg_http_get_var(&hm->query, "order", var_buf, sizeof(var_buf)) > 0) {
            if (strcasecmp(var_buf, "asc") == 0) {
                order = SORT_ORDER_ASC;
            } else if (strcasecmp(var_buf, "desc") == 0) {
                order = SORT_ORDER_DESC;
            }
        }

        if (mg_http_get_var(&hm->query, "limit", var_buf, sizeof(var_buf)) > 0) {
            limit = atoi(var_buf);
        }

        Transaction tx_arr[200];
        int count = 0;
        char err_msg[256];
        if (transaction_processor_get_recent_sorted(config->db_client, session.organization_id, criteria, order, limit, tx_arr, 200, &count, err_msg, sizeof(err_msg)) != TRANSACTION_PROCESSOR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg[0] ? err_msg : "Failed to fetch sorted transactions");
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        size_t json_cap = 262144;
        char *resp_json = (char *)malloc(json_cap);
        if (!resp_json) {
            http_send_json_reply(c, 500, active_origin, config, NULL, "{\"success\":false,\"error\":\"Out of memory\"}");
            return 1;
        }

        const char *crit_str = (criteria == SORT_BY_AMOUNT ? "amount" : (criteria == SORT_BY_ID ? "id" : "timestamp"));
        const char *ord_str = (order == SORT_ORDER_ASC ? "asc" : "desc");

        size_t pos = snprintf(resp_json, json_cap, "{\"success\":true,\"sort_by\":\"%s\",\"order\":\"%s\",\"count\":%d,\"transactions\":[",
                              crit_str, ord_str, count);
        for (int i = 0; i < count; i++) {
            char item_buf[1024];
            transaction_to_json_str(&tx_arr[i], item_buf, sizeof(item_buf));
            pos += snprintf(resp_json + pos, json_cap - pos, "%s%s", (i > 0 ? "," : ""), item_buf);
        }
        snprintf(resp_json + pos, json_cap - pos, "]}");

        http_send_json_reply(c, 200, active_origin, config, NULL, resp_json);
        free(resp_json);
        return 1;
    }

    if (mg_match(hm->uri, mg_str("/api/transactions/recent/search"), NULL)) {
        if (mg_strcmp(hm->method, mg_str("GET")) != 0) {
            http_send_json_reply(c, 405, active_origin, config, NULL, "{\"success\":false,\"error\":\"Method Not Allowed\"}");
            return 1;
        }

        SessionInfo session;
        char raw_session[128];
        if (http_extract_authenticated_session(hm, config->db_client, &session, raw_session, sizeof(raw_session)) != 0) {
            http_send_json_reply(c, 401, active_origin, config, NULL, "{\"success\":false,\"error\":\"Unauthorized or session expired\"}");
            return 1;
        }

        long long target_id = 0;
        char var_buf[32];
        if (mg_http_get_var(&hm->query, "id", var_buf, sizeof(var_buf)) > 0) {
            target_id = atoll(var_buf);
        }

        if (target_id <= 0) {
            http_send_json_reply(c, 400, active_origin, config, NULL, "{\"success\":false,\"error\":\"Missing or invalid id query parameter\"}");
            return 1;
        }

        Transaction tx;
        char err_msg[256];
        int rc = transaction_processor_search_recent_by_id(config->db_client, session.organization_id, target_id, &tx, err_msg, sizeof(err_msg));
        if (rc == TRANSACTION_PROCESSOR_ERR_NOT_FOUND) {
            http_send_json_reply(c, 404, active_origin, config, NULL, "{\"success\":false,\"error\":\"Transaction not found\"}");
            return 1;
        }

        if (rc != TRANSACTION_PROCESSOR_OK) {
            char resp[512];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"error\":\"%s\"}", err_msg[0] ? err_msg : "Search failed");
            http_send_json_reply(c, 500, active_origin, config, NULL, resp);
            return 1;
        }

        char tx_json[1024];
        transaction_to_json_str(&tx, tx_json, sizeof(tx_json));
        char resp[1536];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"transaction\":%s}", tx_json);
        http_send_json_reply(c, 200, active_origin, config, NULL, resp);
        return 1;
    }

    return 0;
}
