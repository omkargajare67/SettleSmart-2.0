#include "transaction_service.h"
#include "../websocket/event_dispatcher.h"
#include "../websocket/websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void set_error(char *err_msg, size_t err_len, const char *msg) {
    if (err_msg && err_len > 0) {
        strncpy(err_msg, msg, err_len - 1);
        err_msg[err_len - 1] = '\0';
    }
}

static void escape_string(DBClient *client, const char *src, char *dest, size_t dest_len) {
    if (!src || !dest || dest_len == 0) {
        return;
    }
    if (client && client->connection) {
        mysql_real_escape_string(client->connection, dest, src, (unsigned long)strlen(src));
    } else {
        strncpy(dest, src, dest_len - 1);
        dest[dest_len - 1] = '\0';
    }
}

static void parse_transaction_row(MYSQL_ROW row, Transaction *t) {
    memset(t, 0, sizeof(Transaction));
    t->id = atoll(row[0]);
    t->organization_id = atoll(row[1]);
    t->sender_entity_id = atoll(row[2]);
    t->receiver_entity_id = atoll(row[3]);
    strncpy(t->amount_str, row[4] ? row[4] : "0.00", sizeof(t->amount_str) - 1);
    char norm[32];
    transaction_parse_amount(t->amount_str, &t->amount_paise, norm, sizeof(norm));
    strncpy(t->currency, row[5] ? row[5] : "INR", sizeof(t->currency) - 1);
    t->status = transaction_status_from_string(row[6]);
    t->transaction_type = transaction_type_from_string(row[7]);
    strncpy(t->idempotency_key, row[8] ? row[8] : "", sizeof(t->idempotency_key) - 1);
    strncpy(t->request_fingerprint, row[9] ? row[9] : "", sizeof(t->request_fingerprint) - 1);
    t->reversal_of_id = atoll(row[10]);
    t->created_by_user_id = atoll(row[11]);
    strncpy(t->description, row[12] ? row[12] : "", sizeof(t->description) - 1);
    strncpy(t->created_at, row[13] ? row[13] : "", sizeof(t->created_at) - 1);
}

int transaction_service_get_by_id(DBClient *client, long long organization_id, long long tx_id,
                                  Transaction *out_tx, char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0 || tx_id <= 0) {
        set_error(err_msg, err_len, "Invalid parameters");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    char query[768];
    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "idempotency_key, request_fingerprint, IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld AND id = %lld;",
             organization_id, tx_id);

    if (db_client_query(client, query) != 0) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        set_error(err_msg, err_len, "Transaction not found");
        return TRANSACTION_ERR_NOT_FOUND;
    }

    if (out_tx) {
        parse_transaction_row(row, out_tx);
    }

    db_client_free_result(res);
    return TRANSACTION_ERR_OK;
}

static int get_by_idempotency_key(DBClient *client, long long organization_id, const char *key,
                                 Transaction *out_tx) {
    char esc_key[128];
    escape_string(client, key, esc_key, sizeof(esc_key));

    char query[768];
    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "idempotency_key, request_fingerprint, IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld AND idempotency_key = '%s';",
             organization_id, esc_key);

    if (db_client_query(client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        return 0;
    }

    if (out_tx) {
        parse_transaction_row(row, out_tx);
    }

    db_client_free_result(res);
    return 1;
}

int transaction_service_create(DBClient *client, long long organization_id, long long user_id,
                               long long sender_id, long long receiver_id,
                               const char *amount_str, const char *currency,
                               const char *idempotency_key,
                               Transaction *out_tx, int *out_is_idempotent_replay,
                               char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0 || user_id <= 0 ||
        sender_id <= 0 || receiver_id <= 0 || !amount_str || !currency || !idempotency_key) {
        set_error(err_msg, err_len, "Missing required parameters");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    if (sender_id == receiver_id) {
        set_error(err_msg, err_len, "Sender and receiver entities must be different");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    int64_t paise = 0;
    char normalized_amount[32];
    if (transaction_parse_amount(amount_str, &paise, normalized_amount, sizeof(normalized_amount)) != 0) {
        set_error(err_msg, err_len, "Invalid financial amount");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    if (transaction_validate_currency(currency) != 0) {
        set_error(err_msg, err_len, "Invalid or unsupported currency");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    if (transaction_validate_idempotency_key(idempotency_key) != 0) {
        set_error(err_msg, err_len, "Invalid Idempotency-Key header: UUID v4 required");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    char fingerprint[65];
    if (transaction_compute_fingerprint(organization_id, sender_id, receiver_id,
                                       normalized_amount, currency, TRANSACTION_TYPE_STANDARD,
                                       fingerprint, sizeof(fingerprint)) != 0) {
        set_error(err_msg, err_len, "Failed to compute request fingerprint");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    Transaction existing_tx;
    int check_existing = get_by_idempotency_key(client, organization_id, idempotency_key, &existing_tx);
    if (check_existing == 1) {
        if (strcmp(existing_tx.request_fingerprint, fingerprint) == 0) {
            if (out_tx) {
                *out_tx = existing_tx;
            }
            if (out_is_idempotent_replay) {
                *out_is_idempotent_replay = 1;
            }
            return TRANSACTION_ERR_OK;
        } else {
            set_error(err_msg, err_len, "Idempotency key collision with different request payload");
            return TRANSACTION_ERR_CONFLICT;
        }
    }

    if (db_client_begin(client) != 0) {
        set_error(err_msg, err_len, "Failed to start database transaction");
        return TRANSACTION_ERR_DB;
    }

    long long first_id = sender_id < receiver_id ? sender_id : receiver_id;
    long long second_id = sender_id < receiver_id ? receiver_id : sender_id;

    char lock_query[256];
    snprintf(lock_query, sizeof(lock_query),
             "SELECT id, status FROM entities WHERE organization_id = %lld AND id IN (%lld, %lld) ORDER BY id FOR UPDATE;",
             organization_id, first_id, second_id);

    if (db_client_query(client, lock_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    int entity_count = 0;
    int all_active = 1;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        entity_count++;
        if (!row[1] || strcmp(row[1], "Active") != 0) {
            all_active = 0;
        }
    }
    db_client_free_result(res);

    if (entity_count != 2) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Sender or receiver entity not found in organization");
        return TRANSACTION_ERR_NOT_FOUND;
    }

    if (!all_active) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Sender or receiver entity is inactive");
        return TRANSACTION_ERR_INACTIVE_ENTITY;
    }

    char esc_key[128];
    escape_string(client, idempotency_key, esc_key, sizeof(esc_key));

    char insert_query[1024];
    snprintf(insert_query, sizeof(insert_query),
             "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, "
             "amount, currency, status, transaction_type, idempotency_key, request_fingerprint, created_by_user_id) "
             "VALUES (%lld, %lld, %lld, %s, '%s', 'Pending', 'Standard', '%s', '%s', %lld);",
             organization_id, sender_id, receiver_id, normalized_amount, currency, esc_key, fingerprint, user_id);

    if (db_client_query(client, insert_query) != 0) {
        unsigned int err_num = mysql_errno(client->connection);
        db_client_rollback(client);

        if (err_num == 1062) {
            Transaction raced_tx;
            if (get_by_idempotency_key(client, organization_id, idempotency_key, &raced_tx) == 1) {
                if (strcmp(raced_tx.request_fingerprint, fingerprint) == 0) {
                    if (out_tx) {
                        *out_tx = raced_tx;
                    }
                    if (out_is_idempotent_replay) {
                        *out_is_idempotent_replay = 1;
                    }
                    return TRANSACTION_ERR_OK;
                } else {
                    set_error(err_msg, err_len, "Idempotency key collision with different request payload");
                    return TRANSACTION_ERR_CONFLICT;
                }
            }
        }

        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    long long tx_id = (long long)db_client_last_insert_id(client);
    Transaction created_tx;
    if (transaction_service_get_by_id(client, organization_id, tx_id, &created_tx, err_msg, err_len) != 0) {
        db_client_rollback(client);
        return TRANSACTION_ERR_DB;
    }

    char payload[2048] = {0};
    event_dispatcher_build_tx_payload(&created_tx, payload, sizeof(payload));

    int64_t event_seq = 0;
    if (outbox_record_event(client, organization_id, tx_id, "TRANSACTION_CREATED", payload, &event_seq) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to record transaction event in outbox");
        return TRANSACTION_ERR_DB;
    }

    if (db_client_commit(client) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to commit transaction");
        return TRANSACTION_ERR_DB;
    }

    char ws_msg[4096];
    if (event_dispatcher_format_ws_message("TRANSACTION_CREATED", event_seq, payload, ws_msg, sizeof(ws_msg)) == 0) {
        ws_broadcast_event(organization_id, ws_msg);
    }

    if (out_tx) {
        *out_tx = created_tx;
    }

    if (out_is_idempotent_replay) {
        *out_is_idempotent_replay = 0;
    }

    return TRANSACTION_ERR_OK;
}

int transaction_service_list(DBClient *client, long long organization_id,
                             const TransactionFilter *filter,
                             Transaction *out_txs, size_t max_txs,
                             TransactionPagination *out_pagination,
                             char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0) {
        set_error(err_msg, err_len, "Invalid parameters");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    int page = (filter && filter->page > 0) ? filter->page : 1;
    int limit = (filter && filter->limit > 0 && filter->limit <= 100) ? filter->limit : 20;

    char where_clause[1024];
    snprintf(where_clause, sizeof(where_clause), "WHERE organization_id = %lld", organization_id);

    if (filter) {
        if (filter->transaction_id > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), " AND id = %lld", filter->transaction_id);
            strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
        }

        if (filter->entity_id > 0) {
            char buf[128];
            if (strcmp(filter->direction, "incoming") == 0) {
                snprintf(buf, sizeof(buf), " AND receiver_entity_id = %lld", filter->entity_id);
            } else if (strcmp(filter->direction, "outgoing") == 0) {
                snprintf(buf, sizeof(buf), " AND sender_entity_id = %lld", filter->entity_id);
            } else {
                snprintf(buf, sizeof(buf), " AND (sender_entity_id = %lld OR receiver_entity_id = %lld)",
                         filter->entity_id, filter->entity_id);
            }
            strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
        }

        if (strlen(filter->from_date) > 0) {
            char esc_from[64];
            escape_string(client, filter->from_date, esc_from, sizeof(esc_from));
            char buf[128];
            snprintf(buf, sizeof(buf), " AND created_at >= '%s'", esc_from);
            strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
        }

        if (strlen(filter->to_date) > 0) {
            char esc_to[64];
            escape_string(client, filter->to_date, esc_to, sizeof(esc_to));
            char buf[128];
            snprintf(buf, sizeof(buf), " AND created_at <= '%s'", esc_to);
            strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
        }

        if (strlen(filter->min_amount) > 0) {
            int64_t p_min = 0;
            char n_min[32];
            if (transaction_parse_amount(filter->min_amount, &p_min, n_min, sizeof(n_min)) == 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), " AND amount >= %s", n_min);
                strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
            }
        }

        if (strlen(filter->max_amount) > 0) {
            int64_t p_max = 0;
            char n_max[32];
            if (transaction_parse_amount(filter->max_amount, &p_max, n_max, sizeof(n_max)) == 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), " AND amount <= %s", n_max);
                strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
            }
        }

        if (strlen(filter->currency) > 0) {
            char esc_curr[16];
            escape_string(client, filter->currency, esc_curr, sizeof(esc_curr));
            char buf[64];
            snprintf(buf, sizeof(buf), " AND currency = '%s'", esc_curr);
            strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
        }

        if (filter->status_filter != TRANSACTION_STATUS_UNKNOWN) {
            const char *st_str = transaction_status_to_string(filter->status_filter);
            char buf[64];
            snprintf(buf, sizeof(buf), " AND status = '%s'", st_str);
            strncat(where_clause, buf, sizeof(where_clause) - strlen(where_clause) - 1);
        }
    }

    char count_query[1200];
    snprintf(count_query, sizeof(count_query), "SELECT COUNT(*) FROM transactions %s;", where_clause);

    if (db_client_query(client, count_query) != 0) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *count_res = db_client_store_result(client);
    if (!count_res) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_ROW count_row = mysql_fetch_row(count_res);
    int total_records = count_row ? atoi(count_row[0]) : 0;
    db_client_free_result(count_res);

    int total_pages = total_records > 0 ? (total_records + limit - 1) / limit : 0;

    if (out_pagination) {
        out_pagination->page = page;
        out_pagination->limit = limit;
        out_pagination->total_records = total_records;
        out_pagination->total_pages = total_pages;
    }

    const char *sort_col = "created_at";
    if (filter) {
        if (strcmp(filter->sort_by, "id") == 0) {
            sort_col = "id";
        } else if (strcmp(filter->sort_by, "amount") == 0) {
            sort_col = "amount";
        } else if (strcmp(filter->sort_by, "created_at") == 0) {
            sort_col = "created_at";
        }
    }

    const char *sort_dir = "DESC";
    if (filter) {
        if (strcasecmp(filter->order, "ASC") == 0) {
            sort_dir = "ASC";
        }
    }

    int offset = (page - 1) * limit;

    char list_query[1500];
    snprintf(list_query, sizeof(list_query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "idempotency_key, request_fingerprint, IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions %s ORDER BY %s %s, id DESC LIMIT %d OFFSET %d;",
             where_clause, sort_col, sort_dir, limit, offset);

    if (db_client_query(client, list_query) != 0) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        if (out_txs && (size_t)count < max_txs) {
            parse_transaction_row(row, &out_txs[count]);
        }
        count++;
    }

    db_client_free_result(res);
    return count;
}

int transaction_service_get_recent(DBClient *client, long long organization_id, int limit,
                                   Transaction *out_txs, size_t max_txs,
                                   int *out_count, char *err_msg, size_t err_len) {
    if (limit <= 0 || limit > 100) {
        limit = 20;
    }

    TransactionFilter filter;
    memset(&filter, 0, sizeof(filter));
    filter.page = 1;
    filter.limit = limit;
    strncpy(filter.sort_by, "created_at", sizeof(filter.sort_by) - 1);
    strncpy(filter.order, "DESC", sizeof(filter.order) - 1);

    TransactionPagination pagi;
    int count = transaction_service_list(client, organization_id, &filter, out_txs, max_txs, &pagi, err_msg, err_len);
    if (count >= 0 && out_count) {
        *out_count = count;
    }
    return count >= 0 ? TRANSACTION_ERR_OK : count;
}

int transaction_service_create_reversal(DBClient *client, long long organization_id, long long user_id,
                                        long long original_tx_id, const char *idempotency_key,
                                        Transaction *out_reversal_tx, int *out_is_idempotent_replay,
                                        char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0 || user_id <= 0 ||
        original_tx_id <= 0 || !idempotency_key) {
        set_error(err_msg, err_len, "Missing required parameters");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    if (transaction_validate_idempotency_key(idempotency_key) != 0) {
        set_error(err_msg, err_len, "Invalid Idempotency-Key header: UUID v4 required");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    if (db_client_begin(client) != 0) {
        set_error(err_msg, err_len, "Failed to start database transaction");
        return TRANSACTION_ERR_DB;
    }

    char idem_query[512];
    snprintf(idem_query, sizeof(idem_query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, idempotency_key, request_fingerprint, IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), description, created_at "
             "FROM transactions WHERE organization_id = %lld AND idempotency_key = '%s' FOR UPDATE;",
             organization_id, idempotency_key);

    if (db_client_query(client, idem_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *idem_res = db_client_store_result(client);
    if (!idem_res) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_ROW idem_row = mysql_fetch_row(idem_res);
    if (idem_row) {
        Transaction existing;
        memset(&existing, 0, sizeof(existing));
        existing.id = atoll(idem_row[0]);
        existing.organization_id = atoll(idem_row[1]);
        existing.sender_entity_id = atoll(idem_row[2]);
        existing.receiver_entity_id = atoll(idem_row[3]);
        strncpy(existing.amount_str, idem_row[4] ? idem_row[4] : "0.00", sizeof(existing.amount_str) - 1);
        char norm[32];
        transaction_parse_amount(existing.amount_str, &existing.amount_paise, norm, sizeof(norm));
        strncpy(existing.currency, idem_row[5] ? idem_row[5] : "INR", sizeof(existing.currency) - 1);
        existing.status = transaction_status_from_string(idem_row[6]);
        existing.transaction_type = transaction_type_from_string(idem_row[7]);
        strncpy(existing.idempotency_key, idem_row[8] ? idem_row[8] : "", sizeof(existing.idempotency_key) - 1);
        strncpy(existing.request_fingerprint, idem_row[9] ? idem_row[9] : "", sizeof(existing.request_fingerprint) - 1);
        existing.reversal_of_id = atoll(idem_row[10]);
        existing.created_by_user_id = atoll(idem_row[11]);
        strncpy(existing.description, idem_row[12] ? idem_row[12] : "", sizeof(existing.description) - 1);
        strncpy(existing.created_at, idem_row[13] ? idem_row[13] : "", sizeof(existing.created_at) - 1);
        db_client_free_result(idem_res);

        if (existing.reversal_of_id == original_tx_id && existing.transaction_type == TRANSACTION_TYPE_REVERSAL) {
            *out_reversal_tx = existing;
            *out_is_idempotent_replay = 1;
            db_client_commit(client);
            return TRANSACTION_ERR_OK;
        } else {
            db_client_rollback(client);
            set_error(err_msg, err_len, "Idempotency key reused for different operation");
            return TRANSACTION_ERR_CONFLICT;
        }
    }
    db_client_free_result(idem_res);

    char orig_query[512];
    snprintf(orig_query, sizeof(orig_query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, IFNULL(reversal_of_id, 0) "
             "FROM transactions WHERE organization_id = %lld AND id = %lld FOR UPDATE;",
             organization_id, original_tx_id);

    if (db_client_query(client, orig_query) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *orig_res = db_client_store_result(client);
    if (!orig_res) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_ROW orig_row = mysql_fetch_row(orig_res);
    if (!orig_row) {
        db_client_free_result(orig_res);
        db_client_rollback(client);
        set_error(err_msg, err_len, "Transaction not found");
        return TRANSACTION_ERR_NOT_FOUND;
    }

    long long orig_sender = atoll(orig_row[2]);
    long long orig_receiver = atoll(orig_row[3]);
    char orig_amount_str[32];
    strncpy(orig_amount_str, orig_row[4] ? orig_row[4] : "0.00", sizeof(orig_amount_str) - 1);
    char orig_currency[4];
    strncpy(orig_currency, orig_row[5] ? orig_row[5] : "INR", sizeof(orig_currency) - 1);
    TransactionStatus orig_status = transaction_status_from_string(orig_row[6]);
    TransactionType orig_type = transaction_type_from_string(orig_row[7]);
    long long existing_reversal_ref = atoll(orig_row[8]);
    db_client_free_result(orig_res);

    if (orig_status != TRANSACTION_STATUS_COMPLETED) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Only completed transactions can be reversed");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    if (orig_type == TRANSACTION_TYPE_REVERSAL || existing_reversal_ref > 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Cannot reverse a reversal transaction");
        return TRANSACTION_ERR_INVALID_PARAM;
    }

    char dup_rev_check[256];
    snprintf(dup_rev_check, sizeof(dup_rev_check),
             "SELECT id FROM transactions WHERE organization_id = %lld AND reversal_of_id = %lld FOR UPDATE;",
             organization_id, original_tx_id);

    if (db_client_query(client, dup_rev_check) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *dup_res = db_client_store_result(client);
    if (!dup_res) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    if (mysql_num_rows(dup_res) > 0) {
        db_client_free_result(dup_res);
        db_client_rollback(client);
        set_error(err_msg, err_len, "Transaction has already been reversed");
        return TRANSACTION_ERR_CONFLICT;
    }
    db_client_free_result(dup_res);

    long long first_id = orig_sender < orig_receiver ? orig_sender : orig_receiver;
    long long second_id = orig_sender < orig_receiver ? orig_receiver : orig_sender;

    char lock_ent[256];
    snprintf(lock_ent, sizeof(lock_ent),
             "SELECT id, status FROM entities WHERE organization_id = %lld AND id IN (%lld, %lld) ORDER BY id FOR UPDATE;",
             organization_id, first_id, second_id);

    if (db_client_query(client, lock_ent) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    MYSQL_RES *ent_res = db_client_store_result(client);
    if (!ent_res) {
        db_client_rollback(client);
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    int ent_count = 0;
    int all_act = 1;
    MYSQL_ROW ent_row;
    while ((ent_row = mysql_fetch_row(ent_res)) != NULL) {
        ent_count++;
        if (!ent_row[1] || strcmp(ent_row[1], "Active") != 0) {
            all_act = 0;
        }
    }
    db_client_free_result(ent_res);

    if (ent_count != 2) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Sender or receiver entity not found in organization");
        return TRANSACTION_ERR_NOT_FOUND;
    }

    if (!all_act) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Sender or receiver entity is inactive");
        return TRANSACTION_ERR_INACTIVE_ENTITY;
    }

    long long rev_sender = orig_receiver;
    long long rev_receiver = orig_sender;

    char rev_fingerprint[65];
    transaction_compute_fingerprint(organization_id, rev_sender, rev_receiver,
                                   orig_amount_str, orig_currency, TRANSACTION_TYPE_REVERSAL,
                                   rev_fingerprint, sizeof(rev_fingerprint));

    char esc_key[128];
    escape_string(client, idempotency_key, esc_key, sizeof(esc_key));

    char insert_rev[1024];
    snprintf(insert_rev, sizeof(insert_rev),
             "INSERT INTO transactions (organization_id, sender_entity_id, receiver_entity_id, "
             "amount, currency, status, transaction_type, idempotency_key, request_fingerprint, reversal_of_id, created_by_user_id) "
             "VALUES (%lld, %lld, %lld, %s, '%s', 'Pending', 'Reversal', '%s', '%s', %lld, %lld);",
             organization_id, rev_sender, rev_receiver, orig_amount_str, orig_currency, esc_key, rev_fingerprint, original_tx_id, user_id);

    if (db_client_query(client, insert_rev) != 0) {
        unsigned int err_num = mysql_errno(client->connection);
        db_client_rollback(client);
        if (err_num == 1062) {
            set_error(err_msg, err_len, "Transaction has already been reversed or duplicate idempotency key");
            return TRANSACTION_ERR_CONFLICT;
        }
        set_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_ERR_DB;
    }

    long long rev_id = (long long)db_client_last_insert_id(client);
    Transaction rev_tx;
    if (transaction_service_get_by_id(client, organization_id, rev_id, &rev_tx, err_msg, err_len) != 0) {
        db_client_rollback(client);
        return TRANSACTION_ERR_DB;
    }

    char payload[2048] = {0};
    event_dispatcher_build_tx_payload(&rev_tx, payload, sizeof(payload));

    int64_t event_seq = 0;
    if (outbox_record_event(client, organization_id, rev_id, "TRANSACTION_CREATED", payload, &event_seq) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to record reversal event in outbox");
        return TRANSACTION_ERR_DB;
    }

    if (db_client_commit(client) != 0) {
        db_client_rollback(client);
        set_error(err_msg, err_len, "Failed to commit reversal transaction");
        return TRANSACTION_ERR_DB;
    }

    char ws_msg[4096];
    if (event_dispatcher_format_ws_message("TRANSACTION_CREATED", event_seq, payload, ws_msg, sizeof(ws_msg)) == 0) {
        ws_broadcast_event(organization_id, ws_msg);
    }

    if (out_reversal_tx) {
        *out_reversal_tx = rev_tx;
    }

    if (out_is_idempotent_replay) {
        *out_is_idempotent_replay = 0;
    }

    return TRANSACTION_ERR_OK;
}
