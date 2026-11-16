#include "stack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int stack_init(Stack *s, size_t capacity) {
    if (!s) {
        return -1;
    }
    s->top = NULL;
    s->size = 0;
    s->capacity = capacity;
    return 0;
}

int stack_push(Stack *s, const Transaction *tx) {
    if (!s || !tx) {
        return -1;
    }

    StackNode *node = (StackNode *)malloc(sizeof(StackNode));
    if (!node) {
        return -1;
    }
    node->tx = *tx;
    node->next = s->top;
    s->top = node;
    s->size++;

    if (s->capacity > 0 && s->size > s->capacity) {
        StackNode *curr = s->top;
        StackNode *prev = NULL;
        while (curr->next) {
            prev = curr;
            curr = curr->next;
        }
        if (prev) {
            prev->next = NULL;
            free(curr);
            s->size--;
        }
    }

    return 0;
}

int stack_pop(Stack *s, Transaction *out_tx) {
    if (!s || !s->top) {
        return -1;
    }

    StackNode *node = s->top;
    if (out_tx) {
        *out_tx = node->tx;
    }

    s->top = node->next;
    free(node);
    s->size--;
    return 0;
}

int stack_peek(const Stack *s, Transaction *out_tx) {
    if (!s || !s->top) {
        return -1;
    }
    if (out_tx) {
        *out_tx = s->top->tx;
    }
    return 0;
}

int stack_is_empty(const Stack *s) {
    return (!s || s->size == 0) ? 1 : 0;
}

size_t stack_get_size(const Stack *s) {
    return s ? s->size : 0;
}

void stack_clear(Stack *s) {
    if (!s) {
        return;
    }

    StackNode *curr = s->top;
    while (curr) {
        StackNode *next = curr->next;
        free(curr);
        curr = next;
    }

    s->top = NULL;
    s->size = 0;
}

void stack_destroy(Stack *s) {
    if (!s) {
        return;
    }
    stack_clear(s);
}
