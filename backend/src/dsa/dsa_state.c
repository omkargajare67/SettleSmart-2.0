#include "dsa_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parse_row_to_tx(MYSQL_ROW row, Transaction *t) {
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

int dsa_state_init(DsaState *state, long long organization_id) {
    if (!state || organization_id <= 0) return -1;
    state->organization_id = organization_id;
    if (linked_list_init(&state->recent_transactions, 500) != 0) return -1;
    if (hashmap_init(&state->entity_balances, 64) != 0) {
        linked_list_destroy(&state->recent_transactions);
        return -1;
    }
    if (queue_init(&state->pending_queue, 500) != 0) {
        linked_list_destroy(&state->recent_transactions);
        hashmap_destroy(&state->entity_balances);
        return -1;
    }
    if (stack_init(&state->completed_stack, 100) != 0) {
        linked_list_destroy(&state->recent_transactions);
        hashmap_destroy(&state->entity_balances);
        queue_destroy(&state->pending_queue);
        return -1;
    }
    if (priority_queue_init(&state->pending_priority_queue, 64) != 0) {
        linked_list_destroy(&state->recent_transactions);
        hashmap_destroy(&state->entity_balances);
        queue_destroy(&state->pending_queue);
        stack_destroy(&state->completed_stack);
        return -1;
    }
    state->is_synchronized = 0;
    return 0;
}

void dsa_state_destroy(DsaState *state) {
    if (!state) return;
    linked_list_destroy(&state->recent_transactions);
    hashmap_destroy(&state->entity_balances);
    queue_destroy(&state->pending_queue);
    stack_destroy(&state->completed_stack);
    priority_queue_destroy(&state->pending_priority_queue);
    state->organization_id = 0;
    state->is_synchronized = 0;
}

int dsa_state_recover_from_db(DsaState *state, DBClient *client) {
    if (!state || !client || !client->connection || state->organization_id <= 0) {
        return -1;
    }

    linked_list_clear(&state->recent_transactions);
    hashmap_clear(&state->entity_balances);
    queue_clear(&state->pending_queue);
    stack_clear(&state->completed_stack);
    priority_queue_clear(&state->pending_priority_queue);
    state->is_synchronized = 0;

    char query[1024];

    snprintf(query, sizeof(query),
             "SELECT id FROM entities WHERE organization_id = %lld;",
             state->organization_id);
    if (db_client_query(client, query) == 0) {
        MYSQL_RES *res = db_client_store_result(client);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                long long entity_id = atoll(row[0]);
                hashmap_insert(&state->entity_balances, entity_id, 0);
            }
            db_client_free_result(res);
        }
    }

    snprintf(query, sizeof(query),
             "SELECT sender_entity_id, receiver_entity_id, amount FROM transactions "
             "WHERE organization_id = %lld AND status IN ('Completed', 'Reversed') ORDER BY id ASC;",
             state->organization_id);
    if (db_client_query(client, query) == 0) {
        MYSQL_RES *res = db_client_store_result(client);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                long long sender_id = atoll(row[0]);
                long long receiver_id = atoll(row[1]);
                int64_t amount_paise = 0;
                char norm[32];
                transaction_parse_amount(row[2] ? row[2] : "0.00", &amount_paise, norm, sizeof(norm));
                hashmap_update_balance(&state->entity_balances, sender_id, -amount_paise);
                hashmap_update_balance(&state->entity_balances, receiver_id, amount_paise);
            }
            db_client_free_result(res);
        }
    }

    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "IFNULL(idempotency_key, ''), IFNULL(request_fingerprint, ''), IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld AND status = 'Pending' "
             "ORDER BY created_at ASC, id ASC LIMIT 500;",
             state->organization_id);
    if (db_client_query(client, query) == 0) {
        MYSQL_RES *res = db_client_store_result(client);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                Transaction tx;
                parse_row_to_tx(row, &tx);
                queue_enqueue(&state->pending_queue, &tx);
                priority_queue_insert(&state->pending_priority_queue, &tx);
            }
            db_client_free_result(res);
        }
    }

    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "IFNULL(idempotency_key, ''), IFNULL(request_fingerprint, ''), IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld AND status = 'Completed' "
             "ORDER BY created_at ASC, id ASC LIMIT 100;",
             state->organization_id);
    if (db_client_query(client, query) == 0) {
        MYSQL_RES *res = db_client_store_result(client);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                Transaction tx;
                parse_row_to_tx(row, &tx);
                stack_push(&state->completed_stack, &tx);
            }
            db_client_free_result(res);
        }
    }

    snprintf(query, sizeof(query),
             "SELECT id, organization_id, sender_entity_id, receiver_entity_id, amount, currency, status, transaction_type, "
             "IFNULL(idempotency_key, ''), IFNULL(request_fingerprint, ''), IFNULL(reversal_of_id, 0), IFNULL(created_by_user_id, 0), "
             "IFNULL(description, ''), DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM transactions WHERE organization_id = %lld "
             "ORDER BY created_at ASC, id ASC LIMIT 500;",
             state->organization_id);
    if (db_client_query(client, query) == 0) {
        MYSQL_RES *res = db_client_store_result(client);
        if (res) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res))) {
                Transaction tx;
                parse_row_to_tx(row, &tx);
                linked_list_insert(&state->recent_transactions, &tx);
            }
            db_client_free_result(res);
        }
    }

    state->is_synchronized = 1;
    return 0;
}

int dsa_state_on_transaction_created(DsaState *state, const Transaction *tx) {
    if (!state || !tx || tx->organization_id != state->organization_id) {
        return -1;
    }

    linked_list_insert(&state->recent_transactions, tx);

    if (tx->status == TRANSACTION_STATUS_PENDING) {
        queue_enqueue(&state->pending_queue, tx);
        priority_queue_insert(&state->pending_priority_queue, tx);
    } else if (tx->status == TRANSACTION_STATUS_COMPLETED) {
        stack_push(&state->completed_stack, tx);
        hashmap_update_balance(&state->entity_balances, tx->sender_entity_id, -tx->amount_paise);
        hashmap_update_balance(&state->entity_balances, tx->receiver_entity_id, tx->amount_paise);
        queue_remove_by_id(&state->pending_queue, tx->id);
        priority_queue_remove_by_id(&state->pending_priority_queue, tx->id);
    }

    return 0;
}

int dsa_state_on_transaction_status_changed(DsaState *state, const Transaction *tx, TransactionStatus old_status) {
    if (!state || !tx || tx->organization_id != state->organization_id) {
        return -1;
    }

    if (old_status == TRANSACTION_STATUS_PENDING) {
        queue_remove_by_id(&state->pending_queue, tx->id);
        priority_queue_remove_by_id(&state->pending_priority_queue, tx->id);
    }

    if (tx->status == TRANSACTION_STATUS_COMPLETED && old_status != TRANSACTION_STATUS_COMPLETED) {
        stack_push(&state->completed_stack, tx);
        hashmap_update_balance(&state->entity_balances, tx->sender_entity_id, -tx->amount_paise);
        hashmap_update_balance(&state->entity_balances, tx->receiver_entity_id, tx->amount_paise);
    }

    ListNode *cur = state->recent_transactions.head;
    while (cur) {
        if (cur->tx.id == tx->id) {
            cur->tx.status = tx->status;
            break;
        }
        cur = cur->next;
    }

    return 0;
}
