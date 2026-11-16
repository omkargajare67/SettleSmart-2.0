#ifndef STACK_H
#define STACK_H

#include "../transactions/transaction.h"
#include <stddef.h>

typedef struct StackNode {
    Transaction tx;
    struct StackNode *next;
} StackNode;

typedef struct {
    StackNode *top;
    size_t size;
    size_t capacity;
} Stack;

int stack_init(Stack *s, size_t capacity);
int stack_push(Stack *s, const Transaction *tx);
int stack_pop(Stack *s, Transaction *out_tx);
int stack_peek(const Stack *s, Transaction *out_tx);
int stack_is_empty(const Stack *s);
size_t stack_get_size(const Stack *s);
void stack_clear(Stack *s);
void stack_destroy(Stack *s);

#endif
