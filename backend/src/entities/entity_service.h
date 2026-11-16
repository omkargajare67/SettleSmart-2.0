#ifndef ENTITY_SERVICE_H
#define ENTITY_SERVICE_H

#include "entity.h"
#include "../database/db_client.h"
#include <stddef.h>

#define ENTITY_ERR_OK 0
#define ENTITY_ERR_INVALID_PARAM -1
#define ENTITY_ERR_CONFLICT -2
#define ENTITY_ERR_NOT_FOUND -3
#define ENTITY_ERR_FORBIDDEN -4
#define ENTITY_ERR_DB -5

typedef struct {
    int page;
    int limit;
    char search[151];
    EntityType type_filter;
    EntityStatus status_filter;
    char sort_by[32];
    char order[8];
} EntityFilter;

typedef struct {
    int page;
    int limit;
    int total_records;
    int total_pages;
} EntityPagination;

int entity_service_create(DBClient *client, long long organization_id, long long user_id,
                          const char *name, EntityType entity_type,
                          Entity *out_entity, char *err_msg, size_t err_len);

int entity_service_get_by_id(DBClient *client, long long organization_id, long long entity_id,
                             Entity *out_entity, char *err_msg, size_t err_len);

int entity_service_list(DBClient *client, long long organization_id, const EntityFilter *filter,
                        Entity *out_entities, size_t max_entities,
                        EntityPagination *out_pagination,
                        char *err_msg, size_t err_len);

int entity_service_update(DBClient *client, long long organization_id, long long user_id,
                          long long entity_id, const char *new_name, EntityType new_type,
                          Entity *out_entity, char *err_msg, size_t err_len);

int entity_service_update_status(DBClient *client, long long organization_id, long long user_id,
                                 long long entity_id, EntityStatus new_status,
                                 Entity *out_entity, char *err_msg, size_t err_len);

#endif
