#ifndef DSA_STATE_H
#define DSA_STATE_H

#include "linked_list.h"
#include "hashmap.h"
#include "queue.h"
#include "stack.h"
#include "priority_queue.h"
#include "../database/db_client.h"

typedef struct {
    long long organization_id;
    LinkedList recent_transactions;
    HashMap entity_balances;
    Queue pending_queue;
    Stack completed_stack;
    PriorityQueue pending_priority_queue;
    int is_synchronized;
} DsaState;

int dsa_state_init(DsaState *state, long long organization_id);
void dsa_state_destroy(DsaState *state);
int dsa_state_recover_from_db(DsaState *state, DBClient *client);
int dsa_state_on_transaction_created(DsaState *state, const Transaction *tx);
int dsa_state_on_transaction_status_changed(DsaState *state, const Transaction *tx, TransactionStatus old_status);

#endif
