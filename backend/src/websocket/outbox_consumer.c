#include "outbox_consumer.h"
#include "event_dispatcher.h"
#include "websocket.h"
#include "../transactions/transaction_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int outbox_consumer_init(OutboxConsumer *consumer, const char *instance_id,
                         DBClient *client, DsaState *optional_state,
                         DirectedGraph *optional_graph) {
    if (!consumer || !instance_id || !client) {
        return -1;
    }
    memset(consumer, 0, sizeof(OutboxConsumer));
    strncpy(consumer->instance_id, instance_id, sizeof(consumer->instance_id) - 1);
    consumer->db_client = client;
    consumer->dsa_state = optional_state;
    consumer->graph = optional_graph;
    consumer->is_running = 1;
    return 0;
}

int outbox_consumer_get_cursor(DBClient *client, const char *instance_id,
                               long long org_id, int64_t *out_seq) {
    if (!client || !client->connection || !instance_id || org_id <= 0 || !out_seq) {
        return -1;
    }
    *out_seq = 0;

    char esc_inst[128];
    mysql_real_escape_string(client->connection, esc_inst, instance_id, (unsigned long)strlen(instance_id));

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT last_consumed_sequence FROM outbox_consumer_cursors "
             "WHERE instance_id = '%s' AND organization_id = %lld;",
             esc_inst, org_id);

    if (db_client_query(client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row && row[0]) {
        *out_seq = atoll(row[0]);
    }
    db_client_free_result(res);
    return 0;
}

int outbox_consumer_set_cursor(DBClient *client, const char *instance_id,
                               long long org_id, int64_t seq) {
    if (!client || !client->connection || !instance_id || org_id <= 0) {
        return -1;
    }

    char esc_inst[128];
    mysql_real_escape_string(client->connection, esc_inst, instance_id, (unsigned long)strlen(instance_id));

    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO outbox_consumer_cursors (instance_id, organization_id, last_consumed_sequence, last_heartbeat) "
             "VALUES ('%s', %lld, %lld, NOW()) "
             "ON DUPLICATE KEY UPDATE last_consumed_sequence = %lld, last_heartbeat = NOW();",
             esc_inst, org_id, seq, seq);

    if (db_client_query(client, query) != 0) {
        return -1;
    }
    return 0;
}

int outbox_consumer_poll_org(OutboxConsumer *consumer, long long org_id,
                             int limit, int *out_events_processed) {
    if (!consumer || !consumer->db_client || !consumer->db_client->connection || org_id <= 0) {
        return -1;
    }
    if (limit <= 0 || limit > 500) {
        limit = 100;
    }

    int64_t cursor = 0;
    if (outbox_consumer_get_cursor(consumer->db_client, consumer->instance_id, org_id, &cursor) != 0) {
        return -1;
    }

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT event_sequence, event_type, transaction_id, payload "
             "FROM event_outbox "
             "WHERE organization_id = %lld AND event_sequence > %lld "
             "ORDER BY event_sequence ASC LIMIT %d;",
             org_id, cursor, limit);

    if (db_client_query(consumer->db_client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(consumer->db_client);
    if (!res) {
        return -1;
    }

    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        int64_t seq = atoll(row[0]);
        const char *ev_type = row[1];
        long long tx_id = atoll(row[2]);
        const char *payload = row[3];

        char ws_msg[4096];
        event_dispatcher_format_ws_message(ev_type, seq, payload, ws_msg, sizeof(ws_msg));
        ws_broadcast_event(org_id, ws_msg);

        if ((consumer->dsa_state || consumer->graph) && tx_id > 0) {
            Transaction tx;
            char tx_err[128];
            if (transaction_service_get_by_id(consumer->db_client, org_id, tx_id, &tx, tx_err, sizeof(tx_err)) == 0) {
                if (strcmp(ev_type, "TRANSACTION_CREATED") == 0) {
                    if (consumer->dsa_state && consumer->dsa_state->organization_id == org_id) {
                        dsa_state_on_transaction_created(consumer->dsa_state, &tx);
                    }
                    if (consumer->graph) {
                        graph_add_edge(consumer->graph, &tx);
                    }
                } else if (strcmp(ev_type, "TRANSACTION_STATUS_CHANGED") == 0) {
                    if (consumer->dsa_state && consumer->dsa_state->organization_id == org_id) {
                        TransactionStatus old_st = (tx.status == TRANSACTION_STATUS_REVERSED)
                                                   ? TRANSACTION_STATUS_COMPLETED
                                                   : TRANSACTION_STATUS_PENDING;
                        dsa_state_on_transaction_status_changed(consumer->dsa_state, &tx, old_st);
                    }
                    if (consumer->graph) {
                        graph_update_edge_status(consumer->graph, tx.id, tx.status);
                    }
                }
            }
        }

        outbox_consumer_set_cursor(consumer->db_client, consumer->instance_id, org_id, seq);
        count++;
    }
    db_client_free_result(res);

    if (out_events_processed) {
        *out_events_processed = count;
    }
    return count;
}

int outbox_consumer_poll_all(OutboxConsumer *consumer, int limit,
                             int *out_events_processed) {
    if (!consumer || !consumer->db_client || !consumer->db_client->connection) {
        return -1;
    }

    const char *query = "SELECT id FROM organizations WHERE status = 'Active';";
    if (db_client_query(consumer->db_client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(consumer->db_client);
    if (!res) {
        return -1;
    }

    long long org_ids[256];
    int org_count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) && org_count < 256) {
        org_ids[org_count++] = atoll(row[0]);
    }
    db_client_free_result(res);

    int total = 0;
    for (int i = 0; i < org_count; i++) {
        int processed = 0;
        outbox_consumer_poll_org(consumer, org_ids[i], limit, &processed);
        total += processed;
    }

    if (out_events_processed) {
        *out_events_processed = total;
    }
    return total;
}

void outbox_consumer_destroy(OutboxConsumer *consumer) {
    if (consumer) {
        consumer->is_running = 0;
    }
}

