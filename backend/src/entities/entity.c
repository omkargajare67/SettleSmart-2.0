#include "entity.h"
#include <ctype.h>
#include <string.h>

const char *entity_type_to_string(EntityType type) {
    switch (type) {
        case ENTITY_TYPE_DEPARTMENT:
            return "Department";
        case ENTITY_TYPE_VENDOR:
            return "Vendor";
        case ENTITY_TYPE_SUBSIDIARY:
            return "Subsidiary";
        case ENTITY_TYPE_CLIENT:
            return "Client";
        case ENTITY_TYPE_PARTNER:
            return "Partner";
        default:
            return "Unknown";
    }
}

EntityType entity_type_from_string(const char *str) {
    if (!str) {
        return ENTITY_TYPE_UNKNOWN;
    }
    if (strcmp(str, "Department") == 0) {
        return ENTITY_TYPE_DEPARTMENT;
    }
    if (strcmp(str, "Vendor") == 0) {
        return ENTITY_TYPE_VENDOR;
    }
    if (strcmp(str, "Subsidiary") == 0) {
        return ENTITY_TYPE_SUBSIDIARY;
    }
    if (strcmp(str, "Client") == 0) {
        return ENTITY_TYPE_CLIENT;
    }
    if (strcmp(str, "Partner") == 0) {
        return ENTITY_TYPE_PARTNER;
    }
    return ENTITY_TYPE_UNKNOWN;
}

const char *entity_status_to_string(EntityStatus status) {
    switch (status) {
        case ENTITY_STATUS_ACTIVE:
            return "Active";
        case ENTITY_STATUS_INACTIVE:
            return "Inactive";
        default:
            return "Unknown";
    }
}

EntityStatus entity_status_from_string(const char *str) {
    if (!str) {
        return ENTITY_STATUS_UNKNOWN;
    }
    if (strcmp(str, "Active") == 0) {
        return ENTITY_STATUS_ACTIVE;
    }
    if (strcmp(str, "Inactive") == 0) {
        return ENTITY_STATUS_INACTIVE;
    }
    return ENTITY_STATUS_UNKNOWN;
}

int entity_validate_name(const char *name, char *cleaned_name, size_t max_len) {
    if (!name) {
        return -1;
    }

    const char *start = name;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (*start == '\0') {
        return -1;
    }

    const char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) {
        end--;
    }

    size_t len = (size_t)(end - start + 1);
    if (len == 0 || len > 150) {
        return -1;
    }

    if (cleaned_name) {
        if (max_len <= len) {
            return -1;
        }
        memcpy(cleaned_name, start, len);
        cleaned_name[len] = '\0';
    }

    return 0;
}

int entity_validate_type(EntityType type) {
    if (type >= ENTITY_TYPE_DEPARTMENT && type <= ENTITY_TYPE_PARTNER) {
        return 0;
    }
    return -1;
}

int entity_validate_status(EntityStatus status) {
    if (status == ENTITY_STATUS_ACTIVE || status == ENTITY_STATUS_INACTIVE) {
        return 0;
    }
    return -1;
}
