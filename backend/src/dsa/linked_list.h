#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include "../transactions/transaction.h"
#include <stddef.h>

typedef struct ListNode {
    Transaction tx;
    struct ListNode *next;
} ListNode;

typedef struct {
    ListNode *head;
    ListNode *tail;
    size_t size;
    size_t capacity;
} LinkedList;

typedef void (*LinkedListVisitor)(const Transaction *tx, void *user_data);

int linked_list_init(LinkedList *list, size_t capacity);
int linked_list_insert(LinkedList *list, const Transaction *tx);
int linked_list_remove_by_id(LinkedList *list, long long tx_id);
const Transaction *linked_list_find_by_id(const LinkedList *list, long long tx_id);
size_t linked_list_get_size(const LinkedList *list);
void linked_list_traverse(const LinkedList *list, LinkedListVisitor visitor, void *user_data);
void linked_list_display(const LinkedList *list);
void linked_list_clear(LinkedList *list);
void linked_list_destroy(LinkedList *list);

#endif
