#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "../transactions/transaction.h"
#include <stddef.h>

typedef struct {
    Transaction *items;
    size_t size;
    size_t capacity;
} PriorityQueue;

int priority_queue_init(PriorityQueue *pq, size_t initial_capacity);
int priority_queue_insert(PriorityQueue *pq, const Transaction *tx);
int priority_queue_extract_max(PriorityQueue *pq, Transaction *out_tx);
int priority_queue_peek(const PriorityQueue *pq, Transaction *out_tx);
int priority_queue_is_empty(const PriorityQueue *pq);
size_t priority_queue_get_size(const PriorityQueue *pq);
int priority_queue_has_id(const PriorityQueue *pq, long long tx_id);
int priority_queue_remove_by_id(PriorityQueue *pq, long long tx_id);
void priority_queue_clear(PriorityQueue *pq);
void priority_queue_destroy(PriorityQueue *pq);

#endif
