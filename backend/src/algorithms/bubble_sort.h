#ifndef BUBBLE_SORT_H
#define BUBBLE_SORT_H

#include "../transactions/transaction.h"
#include <stddef.h>

typedef enum {
    SORT_BY_AMOUNT = 0,
    SORT_BY_TIMESTAMP,
    SORT_BY_ID
} SortCriteria;

typedef enum {
    SORT_ORDER_ASC = 0,
    SORT_ORDER_DESC
} SortOrder;

void bubble_sort_transactions(Transaction *arr, size_t n, SortCriteria criteria, SortOrder order);

#endif
