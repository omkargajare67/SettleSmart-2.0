#ifndef QUEUE_H
#define QUEUE_H

#include "../transactions/transaction.h"
#include <stddef.h>

typedef struct QueueNode {
    Transaction tx;
    struct QueueNode *next;
} QueueNode;

typedef struct {
    QueueNode *front;
    QueueNode *rear;
    size_t size;
    size_t capacity;
} Queue;

int queue_init(Queue *q, size_t capacity);
int queue_enqueue(Queue *q, const Transaction *tx);
int queue_dequeue(Queue *q, Transaction *out_tx);
int queue_peek(const Queue *q, Transaction *out_tx);
int queue_is_empty(const Queue *q);
size_t queue_get_size(const Queue *q);
int queue_has_id(const Queue *q, long long tx_id);
int queue_remove_by_id(Queue *q, long long tx_id);
void queue_clear(Queue *q);
void queue_destroy(Queue *q);

#endif
