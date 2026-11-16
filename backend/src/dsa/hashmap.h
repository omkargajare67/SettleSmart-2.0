#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdint.h>
#include <stddef.h>

typedef struct HashNode {
    long long entity_id;
    int64_t balance_paise;
    struct HashNode *next;
} HashNode;

typedef struct {
    HashNode **buckets;
    size_t bucket_count;
    size_t size;
} HashMap;

int hashmap_init(HashMap *map, size_t bucket_count);
size_t hashmap_hash(long long entity_id, size_t bucket_count);
int hashmap_insert(HashMap *map, long long entity_id, int64_t balance_paise);
int hashmap_update_balance(HashMap *map, long long entity_id, int64_t delta_paise);
int hashmap_get_balance(const HashMap *map, long long entity_id, int64_t *out_balance_paise);
int hashmap_has(const HashMap *map, long long entity_id);
int hashmap_remove(HashMap *map, long long entity_id);
size_t hashmap_get_size(const HashMap *map);
void hashmap_display(const HashMap *map);
void hashmap_clear(HashMap *map);
void hashmap_destroy(HashMap *map);

#endif
