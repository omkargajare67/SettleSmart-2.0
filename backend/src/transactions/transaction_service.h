#ifndef TRANSACTION_SERVICE_H
#define TRANSACTION_SERVICE_H

#include "transaction.h"
#include "../database/db_client.h"
#include <stddef.h>

#define TRANSACTION_ERR_OK 0
#define TRANSACTION_ERR_INVALID_PARAM -1
#define TRANSACTION_ERR_CONFLICT -2
#define TRANSACTION_ERR_NOT_FOUND -3
#define TRANSACTION_ERR_FORBIDDEN -4
#define TRANSACTION_ERR_DB -5
#define TRANSACTION_ERR_INACTIVE_ENTITY -6

typedef struct {
    int page;
    int limit;
    long long transaction_id;
    long long entity_id;
    char direction[16];
    char from_date[32];
    char to_date[32];
    char min_amount[32];
    char max_amount[32];
    char currency[8];
    TransactionStatus status_filter;
    char sort_by[32];
    char order[8];
} TransactionFilter;

typedef struct {
    int page;
    int limit;
    int total_records;
    int total_pages;
} TransactionPagination;

int transaction_service_create(DBClient *client, long long organization_id, long long user_id,
                               long long sender_id, long long receiver_id,
                               const char *amount_str, const char *currency,
                               const char *idempotency_key,
                               Transaction *out_tx, int *out_is_idempotent_replay,
                               char *err_msg, size_t err_len);

int transaction_service_get_by_id(DBClient *client, long long organization_id, long long tx_id,
                                  Transaction *out_tx, char *err_msg, size_t err_len);

int transaction_service_list(DBClient *client, long long organization_id,
                             const TransactionFilter *filter,
                             Transaction *out_txs, size_t max_txs,
                             TransactionPagination *out_pagination,
                             char *err_msg, size_t err_len);

int transaction_service_get_recent(DBClient *client, long long organization_id, int limit,
                                   Transaction *out_txs, size_t max_txs,
                                   int *out_count, char *err_msg, size_t err_len);

int transaction_service_create_reversal(DBClient *client, long long organization_id, long long user_id,
                                        long long original_tx_id, const char *idempotency_key,
                                        Transaction *out_reversal_tx, int *out_is_idempotent_replay,
                                        char *err_msg, size_t err_len);

#endif
