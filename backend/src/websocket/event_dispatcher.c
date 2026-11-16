#include "event_dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int event_dispatcher_build_tx_payload(const Transaction *tx, char *out_payload, size_t max_len) {
    if (!tx || !out_payload || max_len == 0) {
        return -1;
    }

    char rev_buf[32];
    if (tx->reversal_of_id > 0) {
        snprintf(rev_buf, sizeof(rev_buf), "%lld", tx->reversal_of_id);
    } else {
        snprintf(rev_buf, sizeof(rev_buf), "null");
    }

    int written = snprintf(out_payload, max_len,
        "{\"id\":%lld,\"transaction_id\":%lld,\"organization_id\":%lld,"
        "\"sender_id\":%lld,\"sender_entity_id\":%lld,"
        "\"receiver_id\":%lld,\"receiver_entity_id\":%lld,"
        "\"amount_paise\":%lld,\"amount_formatted\":\"%s\",\"amount\":\"%s\","
        "\"currency\":\"%s\",\"status\":\"%s\",\"transaction_type\":\"%s\","
        "\"reversal_of_id\":%s,\"created_at\":\"%s\"}",
        tx->id, tx->id, tx->organization_id,
        tx->sender_entity_id, tx->sender_entity_id,
        tx->receiver_entity_id, tx->receiver_entity_id,
        (long long)tx->amount_paise, tx->amount_str, tx->amount_str,
        tx->currency,
        transaction_status_to_string(tx->status),
        transaction_type_to_string(tx->transaction_type),
        rev_buf, tx->created_at);

    if (written < 0 || (size_t)written >= max_len) {
        return -1;
    }
    return 0;
}

int event_dispatcher_format_ws_message(const char *event_type, int64_t event_seq,
                                       const char *payload_json, char *out_msg, size_t max_len) {
    if (!event_type || !payload_json || !out_msg || max_len == 0) {
        return -1;
    }

    int written = snprintf(out_msg, max_len,
        "{\"type\":\"%s\",\"event_sequence\":%lld,\"payload\":%s,\"transaction\":%s}",
        event_type, (long long)event_seq, payload_json, payload_json);

    if (written < 0 || (size_t)written >= max_len) {
        return -1;
    }
    return 0;
}

int outbox_record_event(DBClient *client, long long org_id, long long tx_id,
                        const char *event_type, const char *payload, int64_t *out_seq) {
    if (!client || !client->connection || org_id <= 0 || tx_id <= 0 || !event_type || !payload) {
        return -1;
    }

    char query[2048];
    snprintf(query, sizeof(query),
             "SELECT event_sequence_counter FROM organizations WHERE id = %lld FOR UPDATE;",
             org_id);
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
        return -1;
    }
    int64_t current_seq = atoll(row[0]);
    db_client_free_result(res);

    int64_t next_seq = current_seq + 1;

    snprintf(query, sizeof(query),
             "UPDATE organizations SET event_sequence_counter = %lld WHERE id = %lld;",
             next_seq, org_id);
    if (db_client_query(client, query) != 0) {
        return -1;
    }

    char esc_payload[4096];
    mysql_real_escape_string(client->connection, esc_payload, payload, (unsigned long)strlen(payload));

    snprintf(query, sizeof(query),
             "INSERT INTO event_outbox (organization_id, event_sequence, event_type, transaction_id, payload) "
             "VALUES (%lld, %lld, '%s', %lld, '%s');",
             org_id, next_seq, event_type, tx_id, esc_payload);
    if (db_client_query(client, query) != 0) {
        return -1;
    }

    if (out_seq) {
        *out_seq = next_seq;
    }
    return 0;
}

int outbox_get_watermark(DBClient *client, long long org_id, int64_t *out_watermark) {
    if (!client || !client->connection || org_id <= 0 || !out_watermark) {
        return -1;
    }

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT event_sequence_counter FROM organizations WHERE id = %lld;",
             org_id);
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
        return -1;
    }
    *out_watermark = atoll(row[0]);
    db_client_free_result(res);
    return 0;
}

int outbox_get_events_after(DBClient *client, long long org_id, int64_t after_seq,
                            int limit, char *out_json, size_t max_len, int *out_count) {
    if (!client || !client->connection || org_id <= 0 || !out_json || max_len == 0) {
        return -1;
    }

    if (limit <= 0 || limit > 500) {
        limit = 100;
    }

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT event_sequence, event_type, payload FROM event_outbox "
             "WHERE organization_id = %lld AND event_sequence > %lld "
             "ORDER BY event_sequence ASC LIMIT %d;",
             org_id, after_seq, limit);

    if (db_client_query(client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        return -1;
    }

    out_json[0] = '[';
    out_json[1] = '\0';
    size_t curr_len = 1;
    int count = 0;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        char item[2048];
        int item_len = snprintf(item, sizeof(item),
            "%s{\"event_sequence\":%lld,\"event_type\":\"%s\",\"transaction\":%s}",
            count > 0 ? "," : "",
            atoll(row[0]),
            row[1],
            row[2]);

        if (item_len > 0 && curr_len + (size_t)item_len + 2 < max_len) {
            strcat(out_json, item);
            curr_len += (size_t)item_len;
            count++;
        }
    }
    db_client_free_result(res);

    strcat(out_json, "]");
    if (out_count) {
        *out_count = count;
    }
    return 0;
}
