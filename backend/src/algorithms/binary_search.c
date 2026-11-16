#include "binary_search.h"

int binary_search_transactions_by_id(const Transaction *arr, size_t n, long long id) {
    if (!arr || n == 0) {
        return -1;
    }

    size_t low = 0;
    size_t high = n - 1;

    while (low <= high) {
        size_t mid = low + (high - low) / 2;

        if (arr[mid].id == id) {
            return (int)mid;
        }

        if (arr[mid].id < id) {
            low = mid + 1;
        } else {
            if (mid == 0) {
                break;
            }
            high = mid - 1;
        }
    }

    return -1;
}

int binary_search_balances_by_entity_id(const EntityBalance *arr, size_t n, long long entity_id) {
    if (!arr || n == 0) {
        return -1;
    }

    size_t low = 0;
    size_t high = n - 1;

    while (low <= high) {
        size_t mid = low + (high - low) / 2;

        if (arr[mid].entity_id == entity_id) {
            return (int)mid;
        }

        if (arr[mid].entity_id < entity_id) {
            low = mid + 1;
        } else {
            if (mid == 0) {
                break;
            }
            high = mid - 1;
        }
    }

    return -1;
}
