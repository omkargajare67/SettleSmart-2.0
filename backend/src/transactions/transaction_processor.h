#ifndef TRANSACTION_PROCESSOR_H
#define TRANSACTION_PROCESSOR_H

#include "transaction.h"
#include "../database/db_client.h"
#include "../dsa/dsa_state.h"
#include "../graph/graph.h"
#include "../algorithms/bubble_sort.h"
#include "../algorithms/binary_search.h"
#include "../algorithms/settlement.h"
#include <stddef.h>
#include <stdint.h>

#define TRANSACTION_PROCESSOR_OK 0
#define TRANSACTION_PROCESSOR_ERR_INVALID_PARAM -1
#define TRANSACTION_PROCESSOR_ERR_NOT_FOUND -2
#define TRANSACTION_PROCESSOR_ERR_INVALID_STATUS -3
#define TRANSACTION_PROCESSOR_ERR_DB -4
#define TRANSACTION_PROCESSOR_ERR_INACTIVE_ENTITY -5
#define TRANSACTION_PROCESSOR_ERR_CONFLICT -6
#define TRANSACTION_PROCESSOR_NO_PENDING 1

typedef struct {
    long long organization_id;
    int pending_count;
    int completed_count;
    int reversed_count;
    int disputed_count;
    int total_count;
} ProcessingStatus;

int transaction_processor_process_tx(DBClient *client, DsaState *optional_state, DirectedGraph *optional_graph,
                                     long long org_id, long long tx_id, Transaction *out_tx,
                                     char *err_msg, size_t err_len);

int transaction_processor_process_next(DBClient *client, DsaState *optional_state, DirectedGraph *optional_graph,
                                       long long org_id, Transaction *out_tx,
                                       char *err_msg, size_t err_len);

int transaction_processor_process_batch(DBClient *client, DsaState *optional_state, DirectedGraph *optional_graph,
                                        long long org_id, int batch_size, int *out_processed_count,
                                        char *err_msg, size_t err_len);

int transaction_processor_get_status(DBClient *client, long long org_id, ProcessingStatus *out_status,
                                     char *err_msg, size_t err_len);

int transaction_processor_get_pending_priority(DBClient *client, long long org_id, int limit,
                                               Transaction *out_txs, size_t max_txs, int *out_count,
                                               char *err_msg, size_t err_len);

int transaction_processor_get_entity_balances(DBClient *client, long long org_id,
                                              EntityBalance **out_balances, size_t *out_count,
                                              char *err_msg, size_t err_len);

void transaction_processor_free_entity_balances(EntityBalance *balances);

int transaction_processor_get_settlement_recommendations(DBClient *client, long long org_id,
                                                         SettlementPlan *out_plan,
                                                         char *err_msg, size_t err_len);

int transaction_processor_get_recent_sorted(DBClient *client, long long org_id,
                                            SortCriteria criteria, SortOrder order, int limit,
                                            Transaction *out_txs, size_t max_txs, int *out_count,
                                            char *err_msg, size_t err_len);

int transaction_processor_search_recent_by_id(DBClient *client, long long org_id,
                                              long long search_tx_id, Transaction *out_tx,
                                              char *err_msg, size_t err_len);

#endif
