#include "priority_queue.h"
#include <stdlib.h>
#include <string.h>

static int is_higher_priority(const Transaction *a, const Transaction *b) {
    if (!a || !b) return 0;
    if (a->amount_paise > b->amount_paise) return 1;
    if (a->amount_paise < b->amount_paise) return 0;

    int cmp = strcmp(a->created_at, b->created_at);
    if (cmp < 0) return 1;
    if (cmp > 0) return 0;

    return a->id < b->id;
}

static void swap_items(Transaction *a, Transaction *b) {
    Transaction temp = *a;
    *a = *b;
    *b = temp;
}

static void heapify_up(PriorityQueue *pq, size_t index) {
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (is_higher_priority(&pq->items[index], &pq->items[parent])) {
            swap_items(&pq->items[index], &pq->items[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

static void heapify_down(PriorityQueue *pq, size_t index) {
    while (1) {
        size_t largest = index;
        size_t left = 2 * index + 1;
        size_t right = 2 * index + 2;

        if (left < pq->size && is_higher_priority(&pq->items[left], &pq->items[largest])) {
            largest = left;
        }

        if (right < pq->size && is_higher_priority(&pq->items[right], &pq->items[largest])) {
            largest = right;
        }

        if (largest != index) {
            swap_items(&pq->items[index], &pq->items[largest]);
            index = largest;
        } else {
            break;
        }
    }
}

int priority_queue_init(PriorityQueue *pq, size_t initial_capacity) {
    if (!pq) return -1;
    if (initial_capacity == 0) initial_capacity = 16;
    pq->items = (Transaction *)malloc(initial_capacity * sizeof(Transaction));
    if (!pq->items) {
        pq->size = 0;
        pq->capacity = 0;
        return -1;
    }
    pq->size = 0;
    pq->capacity = initial_capacity;
    return 0;
}

int priority_queue_insert(PriorityQueue *pq, const Transaction *tx) {
    if (!pq || !tx || !pq->items) return -1;
    if (tx->id > 0 && priority_queue_has_id(pq, tx->id)) return -3;

    if (pq->size == pq->capacity) {
        size_t new_cap = pq->capacity * 2;
        if (new_cap < 16) new_cap = 16;
        Transaction *new_items = (Transaction *)realloc(pq->items, new_cap * sizeof(Transaction));
        if (!new_items) return -1;
        pq->items = new_items;
        pq->capacity = new_cap;
    }

    pq->items[pq->size] = *tx;
    heapify_up(pq, pq->size);
    pq->size++;
    return 0;
}

int priority_queue_extract_max(PriorityQueue *pq, Transaction *out_tx) {
    if (!pq || pq->size == 0 || !pq->items) return -1;

    if (out_tx) {
        *out_tx = pq->items[0];
    }

    pq->size--;
    if (pq->size > 0) {
        pq->items[0] = pq->items[pq->size];
        heapify_down(pq, 0);
    }

    return 0;
}

int priority_queue_peek(const PriorityQueue *pq, Transaction *out_tx) {
    if (!pq || pq->size == 0 || !pq->items) return -1;
    if (out_tx) {
        *out_tx = pq->items[0];
    }
    return 0;
}

int priority_queue_is_empty(const PriorityQueue *pq) {
    return (!pq || pq->size == 0);
}

size_t priority_queue_get_size(const PriorityQueue *pq) {
    return (pq ? pq->size : 0);
}

int priority_queue_has_id(const PriorityQueue *pq, long long tx_id) {
    if (!pq || !pq->items || tx_id <= 0) return 0;
    for (size_t i = 0; i < pq->size; i++) {
        if (pq->items[i].id == tx_id) {
            return 1;
        }
    }
    return 0;
}

int priority_queue_remove_by_id(PriorityQueue *pq, long long tx_id) {
    if (!pq || pq->size == 0 || !pq->items || tx_id <= 0) return -1;

    size_t target_idx = (size_t)-1;
    for (size_t i = 0; i < pq->size; i++) {
        if (pq->items[i].id == tx_id) {
            target_idx = i;
            break;
        }
    }

    if (target_idx == (size_t)-1) return -1;

    pq->size--;
    if (target_idx < pq->size) {
        pq->items[target_idx] = pq->items[pq->size];
        if (target_idx > 0 && is_higher_priority(&pq->items[target_idx], &pq->items[(target_idx - 1) / 2])) {
            heapify_up(pq, target_idx);
        } else {
            heapify_down(pq, target_idx);
        }
    }

    return 0;
}

void priority_queue_clear(PriorityQueue *pq) {
    if (pq) {
        pq->size = 0;
    }
}

void priority_queue_destroy(PriorityQueue *pq) {
    if (pq) {
        if (pq->items) {
            free(pq->items);
            pq->items = NULL;
        }
        pq->size = 0;
        pq->capacity = 0;
    }
}
