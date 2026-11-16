#include "hashmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int hashmap_init(HashMap *map, size_t bucket_count) {
    if (!map) {
        return -1;
    }
    map->bucket_count = bucket_count > 0 ? bucket_count : 64;
    map->buckets = (HashNode **)calloc(map->bucket_count, sizeof(HashNode *));
    if (!map->buckets) {
        return -1;
    }
    map->size = 0;
    return 0;
}

size_t hashmap_hash(long long entity_id, size_t bucket_count) {
    if (bucket_count == 0) {
        return 0;
    }
    uint64_t val = (uint64_t)(entity_id >= 0 ? entity_id : -entity_id);
    return (size_t)(val % bucket_count);
}

int hashmap_insert(HashMap *map, long long entity_id, int64_t balance_paise) {
    if (!map || !map->buckets) {
        return -1;
    }

    size_t idx = hashmap_hash(entity_id, map->bucket_count);
    HashNode *curr = map->buckets[idx];
    while (curr) {
        if (curr->entity_id == entity_id) {
            curr->balance_paise = balance_paise;
            return 0;
        }
        curr = curr->next;
    }

    HashNode *node = (HashNode *)malloc(sizeof(HashNode));
    if (!node) {
        return -1;
    }
    node->entity_id = entity_id;
    node->balance_paise = balance_paise;
    node->next = map->buckets[idx];
    map->buckets[idx] = node;
    map->size++;

    return 0;
}

int hashmap_update_balance(HashMap *map, long long entity_id, int64_t delta_paise) {
    if (!map || !map->buckets) {
        return -1;
    }

    size_t idx = hashmap_hash(entity_id, map->bucket_count);
    HashNode *curr = map->buckets[idx];
    while (curr) {
        if (curr->entity_id == entity_id) {
            if (delta_paise > 0 && curr->balance_paise > INT64_MAX - delta_paise) {
                return -2;
            }
            if (delta_paise < 0 && curr->balance_paise < INT64_MIN - delta_paise) {
                return -2;
            }
            curr->balance_paise += delta_paise;
            return 0;
        }
        curr = curr->next;
    }

    return hashmap_insert(map, entity_id, delta_paise);
}

int hashmap_get_balance(const HashMap *map, long long entity_id, int64_t *out_balance_paise) {
    if (!map || !map->buckets || !out_balance_paise) {
        return -1;
    }

    size_t idx = hashmap_hash(entity_id, map->bucket_count);
    HashNode *curr = map->buckets[idx];
    while (curr) {
        if (curr->entity_id == entity_id) {
            *out_balance_paise = curr->balance_paise;
            return 0;
        }
        curr = curr->next;
    }

    return -1;
}

int hashmap_has(const HashMap *map, long long entity_id) {
    if (!map || !map->buckets) {
        return 0;
    }

    size_t idx = hashmap_hash(entity_id, map->bucket_count);
    HashNode *curr = map->buckets[idx];
    while (curr) {
        if (curr->entity_id == entity_id) {
            return 1;
        }
        curr = curr->next;
    }

    return 0;
}

int hashmap_remove(HashMap *map, long long entity_id) {
    if (!map || !map->buckets) {
        return -1;
    }

    size_t idx = hashmap_hash(entity_id, map->bucket_count);
    HashNode *curr = map->buckets[idx];
    HashNode *prev = NULL;

    while (curr) {
        if (curr->entity_id == entity_id) {
            if (prev) {
                prev->next = curr->next;
            } else {
                map->buckets[idx] = curr->next;
            }
            free(curr);
            map->size--;
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    return -1;
}

size_t hashmap_get_size(const HashMap *map) {
    return map ? map->size : 0;
}

void hashmap_display(const HashMap *map) {
    if (!map || !map->buckets) {
        return;
    }

    for (size_t i = 0; i < map->bucket_count; i++) {
        HashNode *curr = map->buckets[i];
        while (curr) {
            printf("ENTITY[%lld] -> Balance: %lld paise (%.2f)\n",
                   curr->entity_id, (long long)curr->balance_paise,
                   (double)curr->balance_paise / 100.0);
            curr = curr->next;
        }
    }
}

void hashmap_clear(HashMap *map) {
    if (!map || !map->buckets) {
        return;
    }

    for (size_t i = 0; i < map->bucket_count; i++) {
        HashNode *curr = map->buckets[i];
        while (curr) {
            HashNode *next = curr->next;
            free(curr);
            curr = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}

void hashmap_destroy(HashMap *map) {
    if (!map) {
        return;
    }
    hashmap_clear(map);
    if (map->buckets) {
        free(map->buckets);
        map->buckets = NULL;
    }
    map->bucket_count = 0;
}
