#include "transaction_processor.h"
#include "../websocket/event_dispatcher.h"
#include "../websocket/websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_processor_error(char *err_msg, size_t err_len, const char *msg) {
    if (err_msg && err_len > 0) {
        if (msg) {
            strncpy(err_msg, msg, err_len - 1);
            err_msg[err_len - 1] = '\0';
        } else {
            err_msg[0] = '\0';
        }
    }
}

static void parse_row_to_tx_internal(MYSQL_ROW row, Transaction *tx) {
    memset(tx, 0, sizeof(*tx));
    tx->id = atoll(row[0]);
    tx->organization_id = atoll(row[1]);
    tx->sender_entity_id = atoll(row[2]);
    tx->receiver_entity_id = atoll(row[3]);
    strncpy(tx->amount_str, row[4] ? row[4] : "0.00", sizeof(tx->amount_str) - 1);
    char norm[32];
    transaction_parse_amount(tx->amount_str, &tx->amount_paise, norm, sizeof(norm));
    strncpy(tx->currency, row[5] ? row[5] : "INR", sizeof(tx->currency) - 1);
    tx->status = transaction_status_from_string(row[6]);
    tx->transaction_type = transaction_type_from_string(row[7]);
    strncpy(tx->idempotency_key, row[8] ? row[8] : "", sizeof(tx->idempotency_key) - 1);
    strncpy(tx->request_fingerprint, row[9] ? row[9] : "", sizeof(tx->request_fingerprint) - 1);
    tx->reversal_of_id = atoll(row[10]);
    tx->created_by_user_id = atoll(row[11]);
    strncpy(tx->description, row[12] ? row[12] : "", sizeof(tx->description) - 1);
    strncpy(tx->created_at, row[13] ? row[13] : "", sizeof(tx->created_at) - 1);
}

int transaction_processor_process_tx(DBClient *client, DsaState *optional_state, DirectedGraph *optional_graph,
                                     long long org_id, long long tx_id, Transaction *out_tx,
                                     char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0 || tx_id <= 0) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    if (db_client_begin(client) != 0) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    char query[1024];
    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "IFNULL(idempotency_key, ''), IFNULL(request_fingerprint, ''), IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld AND id = %lld FOR UPDATE;",
             org_id, tx_id);

    if (db_client_query(client, query) != 0) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, "Transaction not found");
        return TRANSACTION_PROCESSOR_ERR_NOT_FOUND;
    }

    Transaction tx;
    parse_row_to_tx_internal(row, &tx);
    db_client_free_result(res);

    if (tx.status != TRANSACTION_STATUS_PENDING) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, "Transaction is not in Pending status");
        return TRANSACTION_PROCESSOR_ERR_INVALID_STATUS;
    }

    long long first_id = tx.sender_entity_id < tx.receiver_entity_id ? tx.sender_entity_id : tx.receiver_entity_id;
    long long second_id = tx.sender_entity_id < tx.receiver_entity_id ? tx.receiver_entity_id : tx.sender_entity_id;

    snprintf(query, sizeof(query),
             "SELECT id, status FROM entities WHERE organization_id = %lld AND id IN (%lld, %lld) ORDER BY id FOR UPDATE;",
             org_id, first_id, second_id);

    if (db_client_query(client, query) != 0) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_RES *ent_res = db_client_store_result(client);
    if (!ent_res) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    int ent_count = 0;
    int all_active = 1;
    MYSQL_ROW ent_row;
    while ((ent_row = mysql_fetch_row(ent_res)) != NULL) {
        ent_count++;
        if (!ent_row[1] || strcmp(ent_row[1], "Active") != 0) {
            all_active = 0;
        }
    }
    db_client_free_result(ent_res);

    if (ent_count != 2) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, "Sender or receiver entity not found in organization");
        return TRANSACTION_PROCESSOR_ERR_NOT_FOUND;
    }

    if (!all_active) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, "Sender or receiver entity is inactive");
        return TRANSACTION_PROCESSOR_ERR_INACTIVE_ENTITY;
    }

    int is_reversal = (tx.transaction_type == TRANSACTION_TYPE_REVERSAL || tx.reversal_of_id > 0);
    Transaction orig_tx;
    memset(&orig_tx, 0, sizeof(orig_tx));
    int64_t orig_seq = 0;
    int64_t rev_seq = 0;

    char orig_payload[2048] = {0};
    char rev_payload[2048] = {0};

    if (is_reversal) {
        snprintf(query, sizeof(query),
                 "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
                 "IFNULL(idempotency_key, ''), IFNULL(request_fingerprint, ''), IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
                 "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
                 "FROM transactions WHERE organization_id = %lld AND id = %lld FOR UPDATE;",
                 org_id, tx.reversal_of_id);

        if (db_client_query(client, query) != 0) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, db_client_get_error(client));
            return TRANSACTION_PROCESSOR_ERR_DB;
        }

        MYSQL_RES *orig_res = db_client_store_result(client);
        if (!orig_res) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, db_client_get_error(client));
            return TRANSACTION_PROCESSOR_ERR_DB;
        }

        MYSQL_ROW orig_row = mysql_fetch_row(orig_res);
        if (!orig_row) {
            db_client_free_result(orig_res);
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, "Original transaction not found for reversal");
            return TRANSACTION_PROCESSOR_ERR_NOT_FOUND;
        }

        parse_row_to_tx_internal(orig_row, &orig_tx);
        db_client_free_result(orig_res);

        if (orig_tx.status != TRANSACTION_STATUS_COMPLETED) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, "Only completed transactions can be reversed");
            return TRANSACTION_PROCESSOR_ERR_CONFLICT;
        }

        snprintf(query, sizeof(query),
                 "UPDATE transactions SET status = 'Reversed' WHERE organization_id = %lld AND id = %lld;",
                 org_id, orig_tx.id);
        if (db_client_query(client, query) != 0) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, db_client_get_error(client));
            return TRANSACTION_PROCESSOR_ERR_DB;
        }

        snprintf(query, sizeof(query),
                 "UPDATE transactions SET status = 'Completed' WHERE organization_id = %lld AND id = %lld;",
                 org_id, tx.id);
        if (db_client_query(client, query) != 0) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, db_client_get_error(client));
            return TRANSACTION_PROCESSOR_ERR_DB;
        }

        orig_tx.status = TRANSACTION_STATUS_REVERSED;
        event_dispatcher_build_tx_payload(&orig_tx, orig_payload, sizeof(orig_payload));
        if (outbox_record_event(client, org_id, orig_tx.id, "TRANSACTION_STATUS_CHANGED", orig_payload, &orig_seq) != 0) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, "Failed to record original transaction outbox event");
            return TRANSACTION_PROCESSOR_ERR_DB;
        }

        tx.status = TRANSACTION_STATUS_COMPLETED;
        event_dispatcher_build_tx_payload(&tx, rev_payload, sizeof(rev_payload));
        if (outbox_record_event(client, org_id, tx.id, "TRANSACTION_STATUS_CHANGED", rev_payload, &rev_seq) != 0) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, "Failed to record reversal transaction outbox event");
            return TRANSACTION_PROCESSOR_ERR_DB;
        }
    } else {
        snprintf(query, sizeof(query),
                 "UPDATE transactions SET status = 'Completed' WHERE organization_id = %lld AND id = %lld;",
                 org_id, tx.id);
        if (db_client_query(client, query) != 0) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, db_client_get_error(client));
            return TRANSACTION_PROCESSOR_ERR_DB;
        }

        tx.status = TRANSACTION_STATUS_COMPLETED;
        event_dispatcher_build_tx_payload(&tx, rev_payload, sizeof(rev_payload));
        if (outbox_record_event(client, org_id, tx.id, "TRANSACTION_STATUS_CHANGED", rev_payload, &rev_seq) != 0) {
            db_client_rollback(client);
            set_processor_error(err_msg, err_len, db_client_get_error(client));
            return TRANSACTION_PROCESSOR_ERR_DB;
        }
    }

    if (db_client_commit(client) != 0) {
        db_client_rollback(client);
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    if (is_reversal) {
        if (optional_state) {
            dsa_state_on_transaction_status_changed(optional_state, &orig_tx, TRANSACTION_STATUS_COMPLETED);
            dsa_state_on_transaction_status_changed(optional_state, &tx, TRANSACTION_STATUS_PENDING);
        }
        if (optional_graph) {
            graph_update_edge_status(optional_graph, orig_tx.id, TRANSACTION_STATUS_REVERSED);
            graph_add_edge(optional_graph, &tx);
        }

        char ws_orig[4096];
        if (event_dispatcher_format_ws_message("TRANSACTION_STATUS_CHANGED", orig_seq, orig_payload, ws_orig, sizeof(ws_orig)) == 0) {
            ws_broadcast_event(org_id, ws_orig);
        }

        char ws_rev[4096];
        if (event_dispatcher_format_ws_message("TRANSACTION_STATUS_CHANGED", rev_seq, rev_payload, ws_rev, sizeof(ws_rev)) == 0) {
            ws_broadcast_event(org_id, ws_rev);
        }
    } else {
        if (optional_state) {
            dsa_state_on_transaction_status_changed(optional_state, &tx, TRANSACTION_STATUS_PENDING);
        }
        if (optional_graph) {
            graph_update_edge_status(optional_graph, tx.id, TRANSACTION_STATUS_COMPLETED);
        }

        char ws_msg[4096];
        if (event_dispatcher_format_ws_message("TRANSACTION_STATUS_CHANGED", rev_seq, rev_payload, ws_msg, sizeof(ws_msg)) == 0) {
            ws_broadcast_event(org_id, ws_msg);
        }
    }

    if (out_tx) {
        *out_tx = tx;
    }

    return TRANSACTION_PROCESSOR_OK;
}

int transaction_processor_process_next(DBClient *client, DsaState *optional_state, DirectedGraph *optional_graph,
                                       long long org_id, Transaction *out_tx,
                                       char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id FROM transactions WHERE organization_id = %lld AND status = 'Pending' "
             "ORDER BY created_at ASC, id ASC LIMIT 1;",
             org_id);

    if (db_client_query(client, query) != 0) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        return TRANSACTION_PROCESSOR_NO_PENDING;
    }

    long long next_id = atoll(row[0]);
    db_client_free_result(res);

    return transaction_processor_process_tx(client, optional_state, optional_graph, org_id, next_id, out_tx, err_msg, err_len);
}

int transaction_processor_process_batch(DBClient *client, DsaState *optional_state, DirectedGraph *optional_graph,
                                        long long org_id, int batch_size, int *out_processed_count,
                                        char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    int limit = batch_size <= 0 ? 10 : (batch_size > 100 ? 100 : batch_size);
    int processed = 0;

    for (int i = 0; i < limit; i++) {
        Transaction tx;
        int rc = transaction_processor_process_next(client, optional_state, optional_graph, org_id, &tx, err_msg, err_len);
        if (rc == TRANSACTION_PROCESSOR_NO_PENDING) {
            break;
        }
        if (rc != TRANSACTION_PROCESSOR_OK) {
            if (out_processed_count) {
                *out_processed_count = processed;
            }
            return rc;
        }
        processed++;
    }

    if (out_processed_count) {
        *out_processed_count = processed;
    }

    return TRANSACTION_PROCESSOR_OK;
}

int transaction_processor_get_status(DBClient *client, long long org_id, ProcessingStatus *out_status,
                                     char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0 || !out_status) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    memset(out_status, 0, sizeof(*out_status));
    out_status->organization_id = org_id;

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT status, COUNT(*) FROM transactions WHERE organization_id = %lld GROUP BY status;",
             org_id);

    if (db_client_query(client, query) != 0) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        const char *st_str = row[0];
        int count = atoi(row[1]);

        if (strcmp(st_str, "Pending") == 0) {
            out_status->pending_count = count;
        } else if (strcmp(st_str, "Completed") == 0) {
            out_status->completed_count = count;
        } else if (strcmp(st_str, "Reversed") == 0) {
            out_status->reversed_count = count;
        } else if (strcmp(st_str, "Disputed") == 0) {
            out_status->disputed_count = count;
        }
        out_status->total_count += count;
    }
    db_client_free_result(res);

    return TRANSACTION_PROCESSOR_OK;
}

int transaction_processor_get_pending_priority(DBClient *client, long long org_id, int limit,
                                               Transaction *out_txs, size_t max_txs, int *out_count,
                                               char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0 || !out_txs || max_txs == 0) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    int lim = limit <= 0 ? 50 : (limit > (int)max_txs ? (int)max_txs : limit);

    char query[1024];
    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "IFNULL(idempotency_key, ''), IFNULL(request_fingerprint, ''), IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld AND status = 'Pending' "
             "ORDER BY amount DESC, created_at ASC, id ASC LIMIT %d;",
             org_id, lim);

    if (db_client_query(client, query) != 0) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL && (size_t)count < max_txs) {
        parse_row_to_tx_internal(row, &out_txs[count]);
        count++;
    }
    db_client_free_result(res);

    if (out_count) {
        *out_count = count;
    }

    return TRANSACTION_PROCESSOR_OK;
}

int transaction_processor_get_entity_balances(DBClient *client, long long org_id,
                                              EntityBalance **out_balances, size_t *out_count,
                                              char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0 || !out_balances || !out_count) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    DsaState state;
    if (dsa_state_init(&state, org_id) != 0) {
        set_processor_error(err_msg, err_len, "Failed to initialize DsaState");
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    if (dsa_state_recover_from_db(&state, client) != 0) {
        dsa_state_destroy(&state);
        set_processor_error(err_msg, err_len, "Failed to recover DSA state from database");
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    size_t total_entities = state.entity_balances.size;
    if (total_entities == 0) {
        dsa_state_destroy(&state);
        *out_balances = NULL;
        *out_count = 0;
        return TRANSACTION_PROCESSOR_OK;
    }

    EntityBalance *arr = (EntityBalance *)malloc(total_entities * sizeof(EntityBalance));
    if (!arr) {
        dsa_state_destroy(&state);
        set_processor_error(err_msg, err_len, "Memory allocation failed for entity balances");
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    size_t idx = 0;
    for (size_t b = 0; b < state.entity_balances.bucket_count; b++) {
        HashNode *node = state.entity_balances.buckets[b];
        while (node) {
            if (idx < total_entities) {
                arr[idx].entity_id = node->entity_id;
                arr[idx].balance_paise = node->balance_paise;
                idx++;
            }
            node = node->next;
        }
    }

    dsa_state_destroy(&state);

    if (idx > 1) {
        for (size_t i = 0; i < idx - 1; i++) {
            for (size_t j = 0; j < idx - 1 - i; j++) {
                if (arr[j].entity_id > arr[j + 1].entity_id) {
                    EntityBalance tmp = arr[j];
                    arr[j] = arr[j + 1];
                    arr[j + 1] = tmp;
                }
            }
        }
    }

    *out_balances = arr;
    *out_count = idx;
    return TRANSACTION_PROCESSOR_OK;
}

void transaction_processor_free_entity_balances(EntityBalance *balances) {
    if (balances) {
        free(balances);
    }
}

int transaction_processor_get_settlement_recommendations(DBClient *client, long long org_id,
                                                         SettlementPlan *out_plan,
                                                         char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0 || !out_plan) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    EntityBalance *balances = NULL;
    size_t balance_count = 0;

    int rc = transaction_processor_get_entity_balances(client, org_id, &balances, &balance_count, err_msg, err_len);
    if (rc != TRANSACTION_PROCESSOR_OK) {
        return rc;
    }

    int s_rc = settlement_compute_recommendations(balances, balance_count, out_plan, err_msg, err_len);
    transaction_processor_free_entity_balances(balances);

    if (s_rc != SETTLEMENT_OK) {
        return TRANSACTION_PROCESSOR_ERR_CONFLICT;
    }

    return TRANSACTION_PROCESSOR_OK;
}

int transaction_processor_get_recent_sorted(DBClient *client, long long org_id,
                                            SortCriteria criteria, SortOrder order, int limit,
                                            Transaction *out_txs, size_t max_txs, int *out_count,
                                            char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0 || !out_txs || max_txs == 0) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    int lim = limit <= 0 ? 50 : (limit > (int)max_txs ? (int)max_txs : limit);

    char query[1024];
    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "IFNULL(idempotency_key, ''), IFNULL(request_fingerprint, ''), IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld ORDER BY id DESC LIMIT %d;",
             org_id, lim);

    if (db_client_query(client, query) != 0) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_processor_error(err_msg, err_len, db_client_get_error(client));
        return TRANSACTION_PROCESSOR_ERR_DB;
    }

    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL && (size_t)count < max_txs) {
        parse_row_to_tx_internal(row, &out_txs[count]);
        count++;
    }
    db_client_free_result(res);

    if (count > 1) {
        bubble_sort_transactions(out_txs, (size_t)count, criteria, order);
    }

    if (out_count) {
        *out_count = count;
    }

    return TRANSACTION_PROCESSOR_OK;
}

int transaction_processor_search_recent_by_id(DBClient *client, long long org_id,
                                              long long search_tx_id, Transaction *out_tx,
                                              char *err_msg, size_t err_len) {
    set_processor_error(err_msg, err_len, "");

    if (!client || !client->connection || org_id <= 0 || search_tx_id <= 0 || !out_tx) {
        set_processor_error(err_msg, err_len, "Invalid parameter");
        return TRANSACTION_PROCESSOR_ERR_INVALID_PARAM;
    }

    Transaction tx_pool[256];
    int count = 0;

    int rc = transaction_processor_get_recent_sorted(client, org_id, SORT_BY_ID, SORT_ORDER_ASC, 256,
                                                     tx_pool, 256, &count, err_msg, err_len);
    if (rc != TRANSACTION_PROCESSOR_OK) {
        return rc;
    }

    if (count == 0) {
        set_processor_error(err_msg, err_len, "Transaction not found");
        return TRANSACTION_PROCESSOR_ERR_NOT_FOUND;
    }

    int found_idx = binary_search_transactions_by_id(tx_pool, (size_t)count, search_tx_id);
    if (found_idx < 0) {
        set_processor_error(err_msg, err_len, "Transaction not found in recent records");
        return TRANSACTION_PROCESSOR_ERR_NOT_FOUND;
    }

    *out_tx = tx_pool[found_idx];
    return TRANSACTION_PROCESSOR_OK;
}
