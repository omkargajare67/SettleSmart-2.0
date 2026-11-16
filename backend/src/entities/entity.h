#ifndef ENTITY_H
#define ENTITY_H

#include <stddef.h>

typedef enum {
    ENTITY_TYPE_UNKNOWN = 0,
    ENTITY_TYPE_DEPARTMENT,
    ENTITY_TYPE_VENDOR,
    ENTITY_TYPE_SUBSIDIARY,
    ENTITY_TYPE_CLIENT,
    ENTITY_TYPE_PARTNER
} EntityType;

typedef enum {
    ENTITY_STATUS_UNKNOWN = 0,
    ENTITY_STATUS_ACTIVE,
    ENTITY_STATUS_INACTIVE
} EntityStatus;

typedef struct {
    long long id;
    long long organization_id;
    char name[151];
    EntityType entity_type;
    EntityStatus status;
    long long created_by_user_id;
    long long updated_by_user_id;
    char created_at[32];
    char updated_at[32];
} Entity;

const char *entity_type_to_string(EntityType type);
EntityType entity_type_from_string(const char *str);
const char *entity_status_to_string(EntityStatus status);
EntityStatus entity_status_from_string(const char *str);
int entity_validate_name(const char *name, char *cleaned_name, size_t max_len);
int entity_validate_type(EntityType type);
int entity_validate_status(EntityStatus status);

#endif
