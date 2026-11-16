#include "bubble_sort.h"
#include <string.h>

static int compare_transactions(const Transaction *a, const Transaction *b, SortCriteria criteria) {
    int cmp = 0;

    if (criteria == SORT_BY_AMOUNT) {
        if (a->amount_paise > b->amount_paise) {
            cmp = 1;
        } else if (a->amount_paise < b->amount_paise) {
            cmp = -1;
        } else {
            cmp = strcmp(a->created_at, b->created_at);
            if (cmp == 0) {
                if (a->id > b->id) {
                    cmp = 1;
                } else if (a->id < b->id) {
                    cmp = -1;
                }
            }
        }
    } else if (criteria == SORT_BY_TIMESTAMP) {
        cmp = strcmp(a->created_at, b->created_at);
        if (cmp == 0) {
            if (a->id > b->id) {
                cmp = 1;
            } else if (a->id < b->id) {
                cmp = -1;
            }
        }
    } else {
        if (a->id > b->id) {
            cmp = 1;
        } else if (a->id < b->id) {
            cmp = -1;
        }
    }

    return cmp;
}

void bubble_sort_transactions(Transaction *arr, size_t n, SortCriteria criteria, SortOrder order) {
    if (!arr || n < 2) {
        return;
    }

    for (size_t i = 0; i < n - 1; i++) {
        int swapped = 0;
        for (size_t j = 0; j < n - 1 - i; j++) {
            int cmp = compare_transactions(&arr[j], &arr[j + 1], criteria);
            int need_swap = 0;

            if (order == SORT_ORDER_ASC) {
                if (cmp > 0) {
                    need_swap = 1;
                }
            } else {
                if (cmp < 0) {
                    need_swap = 1;
                }
            }

            if (need_swap) {
                Transaction temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
                swapped = 1;
            }
        }

        if (!swapped) {
            break;
        }
    }
}
