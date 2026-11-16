#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int queue_init(Queue *q, size_t capacity) {
    if (!q) {
        return -1;
    }
    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
    q->capacity = capacity;
    return 0;
}

int queue_has_id(const Queue *q, long long tx_id) {
    if (!q || tx_id <= 0) {
        return 0;
    }
    QueueNode *curr = q->front;
    while (curr) {
        if (curr->tx.id == tx_id) {
            return 1;
        }
        curr = curr->next;
    }
    return 0;
}

int queue_enqueue(Queue *q, const Transaction *tx) {
    if (!q || !tx) {
        return -1;
    }

    if (q->capacity > 0 && q->size >= q->capacity) {
        return -2;
    }

    if (tx->id > 0 && queue_has_id(q, tx->id)) {
        return -3;
    }

    QueueNode *node = (QueueNode *)malloc(sizeof(QueueNode));
    if (!node) {
        return -1;
    }
    node->tx = *tx;
    node->next = NULL;

    if (!q->front) {
        q->front = node;
        q->rear = node;
    } else {
        q->rear->next = node;
        q->rear = node;
    }
    q->size++;

    return 0;
}

int queue_dequeue(Queue *q, Transaction *out_tx) {
    if (!q || !q->front) {
        return -1;
    }

    QueueNode *node = q->front;
    if (out_tx) {
        *out_tx = node->tx;
    }

    q->front = node->next;
    if (!q->front) {
        q->rear = NULL;
    }

    free(node);
    q->size--;
    return 0;
}

int queue_peek(const Queue *q, Transaction *out_tx) {
    if (!q || !q->front) {
        return -1;
    }
    if (out_tx) {
        *out_tx = q->front->tx;
    }
    return 0;
}

int queue_is_empty(const Queue *q) {
    return (!q || q->size == 0) ? 1 : 0;
}

size_t queue_get_size(const Queue *q) {
    return q ? q->size : 0;
}

int queue_remove_by_id(Queue *q, long long tx_id) {
    if (!q || !q->front || tx_id <= 0) {
        return -1;
    }

    QueueNode *curr = q->front;
    QueueNode *prev = NULL;

    while (curr) {
        if (curr->tx.id == tx_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                q->front = curr->next;
            }

            if (curr == q->rear) {
                q->rear = prev;
            }

            free(curr);
            q->size--;
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    return -1;
}

void queue_clear(Queue *q) {
    if (!q) {
        return;
    }

    QueueNode *curr = q->front;
    while (curr) {
        QueueNode *next = curr->next;
        free(curr);
        curr = next;
    }

    q->front = NULL;
    q->rear = NULL;
    q->size = 0;
}

void queue_destroy(Queue *q) {
    if (!q) {
        return;
    }
    queue_clear(q);
}
