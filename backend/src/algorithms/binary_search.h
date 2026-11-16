#ifndef BINARY_SEARCH_H
#define BINARY_SEARCH_H

#include "../transactions/transaction.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    long long entity_id;
    int64_t balance_paise;
} EntityBalance;

int binary_search_transactions_by_id(const Transaction *arr, size_t n, long long id);
int binary_search_balances_by_entity_id(const EntityBalance *arr, size_t n, long long entity_id);

#endif
