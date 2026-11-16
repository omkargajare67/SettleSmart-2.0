#include "graph_service.h"
#include "../websocket/event_dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void format_paise_str(int64_t paise, char *out, size_t out_len) {
    long long whole = (long long)(paise / 100);
    long long frac = (long long)(paise % 100);
    if (frac < 0) {
        frac = -frac;
    }
    snprintf(out, out_len, "%lld.%02lld", whole, frac);
}

int graph_service_recover_from_db(DirectedGraph *g, DBClient *client, long long org_id, size_t limit) {
    if (!g || !client || !client->connection || org_id <= 0) {
        return -1;
    }

    char ent_query[512];
    snprintf(ent_query, sizeof(ent_query),
             "SELECT id, name, entity_type, status FROM entities WHERE organization_id = %lld AND status = 'Active' ORDER BY id ASC;",
             org_id);

    if (db_client_query(client, ent_query) != 0) {
        return -1;
    }

    MYSQL_RES *ent_res = db_client_store_result(client);
    if (!ent_res) {
        return -1;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(ent_res))) {
        long long ent_id = atoll(row[0]);
        const char *name = row[1] ? row[1] : "";
        const char *type_str = row[2] ? row[2] : "Company";
        graph_get_or_create_node(g, ent_id, name, type_str);
    }
    db_client_free_result(ent_res);

    size_t edge_limit = limit;
    if (edge_limit == 0 || edge_limit > g->max_edge_capacity) {
        edge_limit = g->max_edge_capacity;
    }

    char tx_query[512];
    snprintf(tx_query, sizeof(tx_query),
             "SELECT id, sender_entity_id, receiver_entity_id, amount, currency, status, reversal_of_id, created_at "
             "FROM transactions WHERE organization_id = %lld ORDER BY created_at ASC, id ASC LIMIT %zu;",
             org_id, edge_limit);

    if (db_client_query(client, tx_query) != 0) {
        return -1;
    }

    MYSQL_RES *tx_res = db_client_store_result(client);
    if (!tx_res) {
        return -1;
    }

    while ((row = mysql_fetch_row(tx_res))) {
        Transaction tx;
        memset(&tx, 0, sizeof(Transaction));
        tx.id = atoll(row[0]);
        tx.organization_id = org_id;
        tx.sender_entity_id = atoll(row[1]);
        tx.receiver_entity_id = atoll(row[2]);
        transaction_parse_amount(row[3] ? row[3] : "0.00", &tx.amount_paise, tx.amount_str, sizeof(tx.amount_str));
        snprintf(tx.currency, sizeof(tx.currency), "%s", row[4] ? row[4] : "INR");
        tx.status = transaction_status_from_string(row[5] ? row[5] : "Completed");
        tx.reversal_of_id = row[6] ? atoll(row[6]) : 0;
        if (row[7]) {
            strncpy(tx.created_at, row[7], sizeof(tx.created_at) - 1);
        }

        graph_add_edge(g, &tx);
    }
    db_client_free_result(tx_res);

    return 0;
}

int graph_service_get_snapshot_json(DBClient *client, long long org_id, long long entity_id,
                                    const char *status, const char *from_date, const char *to_date,
                                    int page, int limit, char *out_json, size_t max_len) {
    if (!client || !client->connection || org_id <= 0 || !out_json || max_len == 0) {
        return -1;
    }

    if (page < 1) {
        page = 1;
    }
    if (limit < 1) {
        limit = 100;
    }
    if (limit > 500) {
        limit = 500;
    }

    db_client_query(client, "START TRANSACTION WITH CONSISTENT SNAPSHOT;");

    int64_t watermark = 0;
    outbox_get_watermark(client, org_id, &watermark);

    char where_tx[1024];
    snprintf(where_tx, sizeof(where_tx), "organization_id = %lld", org_id);

    if (entity_id > 0) {
        char ent_part[128];
        snprintf(ent_part, sizeof(ent_part), " AND (sender_entity_id = %lld OR receiver_entity_id = %lld)",
                 entity_id, entity_id);
        strncat(where_tx, ent_part, sizeof(where_tx) - strlen(where_tx) - 1);
    }

    if (status && strlen(status) > 0) {
        char esc_status[128];
        mysql_real_escape_string(client->connection, esc_status, status, (unsigned long)strlen(status));
        char st_part[160];
        snprintf(st_part, sizeof(st_part), " AND status = '%s'", esc_status);
        strncat(where_tx, st_part, sizeof(where_tx) - strlen(where_tx) - 1);
    }

    if (from_date && strlen(from_date) > 0) {
        char esc_from[128];
        mysql_real_escape_string(client->connection, esc_from, from_date, (unsigned long)strlen(from_date));
        char from_part[160];
        snprintf(from_part, sizeof(from_part), " AND created_at >= '%s'", esc_from);
        strncat(where_tx, from_part, sizeof(where_tx) - strlen(where_tx) - 1);
    }

    if (to_date && strlen(to_date) > 0) {
        char esc_to[128];
        mysql_real_escape_string(client->connection, esc_to, to_date, (unsigned long)strlen(to_date));
        char to_part[160];
        snprintf(to_part, sizeof(to_part), " AND created_at <= '%s'", esc_to);
        strncat(where_tx, to_part, sizeof(where_tx) - strlen(where_tx) - 1);
    }

    char count_q[1200];
    snprintf(count_q, sizeof(count_q), "SELECT COUNT(*) FROM transactions WHERE %s;", where_tx);

    if (db_client_query(client, count_q) != 0) {
        return -1;
    }

    MYSQL_RES *count_res = db_client_store_result(client);
    if (!count_res) {
        return -1;
    }

    int total_edges = 0;
    MYSQL_ROW c_row = mysql_fetch_row(count_res);
    if (c_row && c_row[0]) {
        total_edges = atoi(c_row[0]);
    }
    db_client_free_result(count_res);

    int total_pages = (total_edges + limit - 1) / limit;
    if (total_pages == 0) {
        total_pages = 1;
    }
    int offset = (page - 1) * limit;

    char ent_query[512];
    if (entity_id > 0) {
        snprintf(ent_query, sizeof(ent_query),
                 "SELECT id, name, entity_type, status FROM entities WHERE organization_id = %lld AND (id = %lld OR status = 'Active') ORDER BY id ASC;",
                 org_id, entity_id);
    } else {
        snprintf(ent_query, sizeof(ent_query),
                 "SELECT id, name, entity_type, status FROM entities WHERE organization_id = %lld AND status = 'Active' ORDER BY id ASC;",
                 org_id);
    }

    if (db_client_query(client, ent_query) != 0) {
        return -1;
    }

    MYSQL_RES *ent_res = db_client_store_result(client);
    if (!ent_res) {
        return -1;
    }

    size_t nodes_cap = 65536;
    char *nodes_json = (char *)malloc(nodes_cap);
    if (!nodes_json) {
        db_client_free_result(ent_res);
        return -1;
    }
    nodes_json[0] = '[';
    nodes_json[1] = '\0';

    int first_node = 1;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(ent_res))) {
        char item[512];
        snprintf(item, sizeof(item),
                 "%s{\"id\":%s,\"name\":\"%s\",\"type\":\"%s\",\"status\":\"%s\"}",
                 first_node ? "" : ",",
                 row[0] ? row[0] : "0",
                 row[1] ? row[1] : "",
                 row[2] ? row[2] : "Company",
                 row[3] ? row[3] : "Active");
        first_node = 0;
        if (strlen(nodes_json) + strlen(item) + 2 < nodes_cap) {
            strcat(nodes_json, item);
        }
    }
    strcat(nodes_json, "]");
    db_client_free_result(ent_res);

    char tx_query[1400];
    snprintf(tx_query, sizeof(tx_query),
             "SELECT id, sender_entity_id, receiver_entity_id, amount, currency, status, reversal_of_id, created_at "
             "FROM transactions WHERE %s ORDER BY created_at ASC, id ASC LIMIT %d OFFSET %d;",
             where_tx, limit, offset);

    if (db_client_query(client, tx_query) != 0) {
        free(nodes_json);
        return -1;
    }

    MYSQL_RES *tx_res = db_client_store_result(client);
    if (!tx_res) {
        free(nodes_json);
        return -1;
    }

    size_t edges_cap = 131072;
    char *edges_json = (char *)malloc(edges_cap);
    if (!edges_json) {
        free(nodes_json);
        db_client_free_result(tx_res);
        return -1;
    }
    edges_json[0] = '[';
    edges_json[1] = '\0';

    int first_edge = 1;
    while ((row = mysql_fetch_row(tx_res))) {
        long long t_id = atoll(row[0]);
        long long s_id = atoll(row[1]);
        long long r_id = atoll(row[2]);
        int64_t a_paise = 0;
        char norm_amt[32];
        transaction_parse_amount(row[3] ? row[3] : "0.00", &a_paise, norm_amt, sizeof(norm_amt));
        const char *curr = row[4] ? row[4] : "INR";
        const char *st = row[5] ? row[5] : "Completed";
        char rev_buf[32];
        if (row[6] && strlen(row[6]) > 0 && strcmp(row[6], "0") != 0) {
            snprintf(rev_buf, sizeof(rev_buf), "%s", row[6]);
        } else {
            snprintf(rev_buf, sizeof(rev_buf), "null");
        }
        const char *c_at = row[7] ? row[7] : "";

        char fmt_amt[64];
        format_paise_str(a_paise, fmt_amt, sizeof(fmt_amt));

        char item[512];
        snprintf(item, sizeof(item),
                 "%s{\"id\":%lld,\"sender_id\":%lld,\"receiver_id\":%lld,\"amount_paise\":%lld,\"amount_formatted\":\"%s\",\"currency\":\"%s\",\"status\":\"%s\",\"reversal_of_id\":%s,\"created_at\":\"%s\"}",
                 first_edge ? "" : ",",
                 t_id, s_id, r_id, (long long)a_paise, fmt_amt, curr, st, rev_buf, c_at);
        first_edge = 0;
        if (strlen(edges_json) + strlen(item) + 2 < edges_cap) {
            strcat(edges_json, item);
        }
    }
    strcat(edges_json, "]");
    db_client_free_result(tx_res);

    int written = snprintf(out_json, max_len,
                           "{\"success\":true,\"event_sequence_watermark\":%lld,\"nodes\":%s,\"edges\":%s,\"pagination\":{\"page\":%d,\"limit\":%d,\"total_edges\":%d,\"total_pages\":%d}}",
                           (long long)watermark, nodes_json, edges_json, page, limit, total_edges, total_pages);

    free(nodes_json);
    free(edges_json);

    db_client_commit(client);

    if (written < 0 || (size_t)written >= max_len) {
        return -1;
    }

    return 0;
}
