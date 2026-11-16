#include "linked_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int linked_list_init(LinkedList *list, size_t capacity) {
    if (!list) {
        return -1;
    }
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    list->capacity = capacity > 0 ? capacity : 500;
    return 0;
}

int linked_list_insert(LinkedList *list, const Transaction *tx) {
    if (!list || !tx) {
        return -1;
    }

    ListNode *node = (ListNode *)malloc(sizeof(ListNode));
    if (!node) {
        return -1;
    }
    node->tx = *tx;
    node->next = list->head;
    list->head = node;

    if (!list->tail) {
        list->tail = node;
    }
    list->size++;

    if (list->capacity > 0 && list->size > list->capacity) {
        ListNode *curr = list->head;
        ListNode *prev = NULL;
        while (curr->next) {
            prev = curr;
            curr = curr->next;
        }
        if (prev) {
            prev->next = NULL;
            list->tail = prev;
        } else {
            list->head = NULL;
            list->tail = NULL;
        }
        free(curr);
        list->size--;
    }

    return 0;
}

int linked_list_remove_by_id(LinkedList *list, long long tx_id) {
    if (!list || !list->head || tx_id <= 0) {
        return -1;
    }

    ListNode *curr = list->head;
    ListNode *prev = NULL;

    while (curr) {
        if (curr->tx.id == tx_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                list->head = curr->next;
            }

            if (curr == list->tail) {
                list->tail = prev;
            }

            free(curr);
            list->size--;
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    return -1;
}

const Transaction *linked_list_find_by_id(const LinkedList *list, long long tx_id) {
    if (!list || tx_id <= 0) {
        return NULL;
    }

    ListNode *curr = list->head;
    while (curr) {
        if (curr->tx.id == tx_id) {
            return &curr->tx;
        }
        curr = curr->next;
    }
    return NULL;
}

size_t linked_list_get_size(const LinkedList *list) {
    if (!list) {
        return 0;
    }
    return list->size;
}

void linked_list_traverse(const LinkedList *list, LinkedListVisitor visitor, void *user_data) {
    if (!list || !visitor) {
        return;
    }

    ListNode *curr = list->head;
    while (curr) {
        visitor(&curr->tx, user_data);
        curr = curr->next;
    }
}

void linked_list_display(const LinkedList *list) {
    if (!list) {
        return;
    }

    ListNode *curr = list->head;
    while (curr) {
        printf("TX[id=%lld, org=%lld, sender=%lld, receiver=%lld, amount=%s, status=%s]\n",
               curr->tx.id, curr->tx.organization_id, curr->tx.sender_entity_id,
               curr->tx.receiver_entity_id, curr->tx.amount_str,
               transaction_status_to_string(curr->tx.status));
        curr = curr->next;
    }
}

void linked_list_clear(LinkedList *list) {
    if (!list) {
        return;
    }

    ListNode *curr = list->head;
    while (curr) {
        ListNode *next = curr->next;
        free(curr);
        curr = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void linked_list_destroy(LinkedList *list) {
    if (!list) {
        return;
    }
    linked_list_clear(list);
    list->capacity = 0;
}
