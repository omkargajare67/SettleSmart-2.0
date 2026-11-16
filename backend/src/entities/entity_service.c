#include "entity_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void set_error(char *err_msg, size_t err_len, const char *msg) {
    if (err_msg && err_len > 0) {
        strncpy(err_msg, msg, err_len - 1);
        err_msg[err_len - 1] = '\0';
    }
}

static void escape_string(DBClient *client, const char *src, char *dest, size_t dest_len) {
    if (!src || !dest || dest_len == 0) {
        return;
    }
    if (client && client->connection) {
        mysql_real_escape_string(client->connection, dest, src, (unsigned long)strlen(src));
    } else {
        strncpy(dest, src, dest_len - 1);
        dest[dest_len - 1] = '\0';
    }
}

static int is_all_digits(const char *str) {
    if (!str || *str == '\0') {
        return 0;
    }
    while (*str) {
        if (!isdigit((unsigned char)*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

int entity_service_get_by_id(DBClient *client, long long organization_id, long long entity_id,
                             Entity *out_entity, char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0 || entity_id <= 0) {
        set_error(err_msg, err_len, "Invalid parameters");
        return ENTITY_ERR_INVALID_PARAM;
    }

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id, organization_id, name, entity_type, status, "
             "IFNULL(created_by_user_id, 0), IFNULL(updated_by_user_id, 0), "
             "DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s'), "
             "DATE_FORMAT(updated_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM entities WHERE organization_id = %lld AND id = %lld;",
             organization_id, entity_id);

    if (db_client_query(client, query) != 0) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) {
        db_client_free_result(res);
        set_error(err_msg, err_len, "Entity not found");
        return ENTITY_ERR_NOT_FOUND;
    }

    if (out_entity) {
        memset(out_entity, 0, sizeof(Entity));
        out_entity->id = atoll(row[0]);
        out_entity->organization_id = atoll(row[1]);
        strncpy(out_entity->name, row[2] ? row[2] : "", sizeof(out_entity->name) - 1);
        out_entity->entity_type = entity_type_from_string(row[3]);
        out_entity->status = entity_status_from_string(row[4]);
        out_entity->created_by_user_id = atoll(row[5]);
        out_entity->updated_by_user_id = atoll(row[6]);
        strncpy(out_entity->created_at, row[7] ? row[7] : "", sizeof(out_entity->created_at) - 1);
        strncpy(out_entity->updated_at, row[8] ? row[8] : "", sizeof(out_entity->updated_at) - 1);
    }

    db_client_free_result(res);
    return ENTITY_ERR_OK;
}

int entity_service_create(DBClient *client, long long organization_id, long long user_id,
                          const char *name, EntityType entity_type,
                          Entity *out_entity, char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0 || user_id <= 0) {
        set_error(err_msg, err_len, "Invalid parameters");
        return ENTITY_ERR_INVALID_PARAM;
    }

    char cleaned_name[151];
    if (entity_validate_name(name, cleaned_name, sizeof(cleaned_name)) != 0) {
        set_error(err_msg, err_len, "Entity name must be between 1 and 150 characters");
        return ENTITY_ERR_INVALID_PARAM;
    }

    if (entity_validate_type(entity_type) != 0) {
        set_error(err_msg, err_len, "Invalid entity type");
        return ENTITY_ERR_INVALID_PARAM;
    }

    char esc_name[305];
    escape_string(client, cleaned_name, esc_name, sizeof(esc_name));

    const char *type_str = entity_type_to_string(entity_type);

    char query[768];
    snprintf(query, sizeof(query),
             "INSERT INTO entities (organization_id, name, entity_type, status, created_by_user_id, updated_by_user_id) "
             "VALUES (%lld, '%s', '%s', 'Active', %lld, %lld);",
             organization_id, esc_name, type_str, user_id, user_id);

    if (db_client_query(client, query) != 0) {
        unsigned int err_num = mysql_errno(client->connection);
        if (err_num == 1062) {
            set_error(err_msg, err_len, "Entity with this name already exists in organization");
            return ENTITY_ERR_CONFLICT;
        }
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    long long insert_id = (long long)db_client_last_insert_id(client);
    return entity_service_get_by_id(client, organization_id, insert_id, out_entity, err_msg, err_len);
}

int entity_service_list(DBClient *client, long long organization_id, const EntityFilter *filter,
                        Entity *out_entities, size_t max_entities,
                        EntityPagination *out_pagination,
                        char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0) {
        set_error(err_msg, err_len, "Invalid parameters");
        return ENTITY_ERR_INVALID_PARAM;
    }

    int page = (filter && filter->page > 0) ? filter->page : 1;
    int limit = (filter && filter->limit > 0 && filter->limit <= 100) ? filter->limit : 10;

    char where_clause[1024];
    snprintf(where_clause, sizeof(where_clause), "WHERE organization_id = %lld", organization_id);

    if (filter) {
        if (strlen(filter->search) > 0) {
            char esc_search[305];
            escape_string(client, filter->search, esc_search, sizeof(esc_search));
            char search_cond[512];
            if (is_all_digits(filter->search)) {
                snprintf(search_cond, sizeof(search_cond),
                         " AND (name LIKE '%%%s%%' OR id = %s)", esc_search, filter->search);
            } else {
                snprintf(search_cond, sizeof(search_cond),
                         " AND (name LIKE '%%%s%%')", esc_search);
            }
            strncat(where_clause, search_cond, sizeof(where_clause) - strlen(where_clause) - 1);
        }

        if (filter->type_filter != ENTITY_TYPE_UNKNOWN) {
            const char *type_str = entity_type_to_string(filter->type_filter);
            char type_cond[128];
            snprintf(type_cond, sizeof(type_cond), " AND entity_type = '%s'", type_str);
            strncat(where_clause, type_cond, sizeof(where_clause) - strlen(where_clause) - 1);
        }

        if (filter->status_filter != ENTITY_STATUS_UNKNOWN) {
            const char *status_str = entity_status_to_string(filter->status_filter);
            char status_cond[128];
            snprintf(status_cond, sizeof(status_cond), " AND status = '%s'", status_str);
            strncat(where_clause, status_cond, sizeof(where_clause) - strlen(where_clause) - 1);
        }
    }

    char count_query[1200];
    snprintf(count_query, sizeof(count_query), "SELECT COUNT(*) FROM entities %s;", where_clause);

    if (db_client_query(client, count_query) != 0) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    MYSQL_RES *count_res = db_client_store_result(client);
    if (!count_res) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    MYSQL_ROW count_row = mysql_fetch_row(count_res);
    int total_records = count_row ? atoi(count_row[0]) : 0;
    db_client_free_result(count_res);

    int total_pages = 0;
    if (total_records > 0) {
        total_pages = (total_records + limit - 1) / limit;
    }

    if (out_pagination) {
        out_pagination->page = page;
        out_pagination->limit = limit;
        out_pagination->total_records = total_records;
        out_pagination->total_pages = total_pages;
    }

    const char *sort_col = "id";
    if (filter) {
        if (strcmp(filter->sort_by, "name") == 0) {
            sort_col = "name";
        } else if (strcmp(filter->sort_by, "created_at") == 0) {
            sort_col = "created_at";
        } else if (strcmp(filter->sort_by, "id") == 0) {
            sort_col = "id";
        }
    }

    const char *sort_dir = "ASC";
    if (filter) {
        if (strcasecmp(filter->order, "DESC") == 0) {
            sort_dir = "DESC";
        }
    }

    int offset = (page - 1) * limit;

    char list_query[1500];
    snprintf(list_query, sizeof(list_query),
             "SELECT id, organization_id, name, entity_type, status, "
             "IFNULL(created_by_user_id, 0), IFNULL(updated_by_user_id, 0), "
             "DATE_FORMAT(created_at, '%%Y-%%m-%%d %%H:%%i:%%s'), "
             "DATE_FORMAT(updated_at, '%%Y-%%m-%%d %%H:%%i:%%s') "
             "FROM entities %s ORDER BY %s %s LIMIT %d OFFSET %d;",
             where_clause, sort_col, sort_dir, limit, offset);

    if (db_client_query(client, list_query) != 0) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    int count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        if (out_entities && (size_t)count < max_entities) {
            Entity *e = &out_entities[count];
            memset(e, 0, sizeof(Entity));
            e->id = atoll(row[0]);
            e->organization_id = atoll(row[1]);
            strncpy(e->name, row[2] ? row[2] : "", sizeof(e->name) - 1);
            e->entity_type = entity_type_from_string(row[3]);
            e->status = entity_status_from_string(row[4]);
            e->created_by_user_id = atoll(row[5]);
            e->updated_by_user_id = atoll(row[6]);
            strncpy(e->created_at, row[7] ? row[7] : "", sizeof(e->created_at) - 1);
            strncpy(e->updated_at, row[8] ? row[8] : "", sizeof(e->updated_at) - 1);
        }
        count++;
    }

    db_client_free_result(res);
    return count;
}

int entity_service_update(DBClient *client, long long organization_id, long long user_id,
                          long long entity_id, const char *new_name, EntityType new_type,
                          Entity *out_entity, char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0 || user_id <= 0 || entity_id <= 0) {
        set_error(err_msg, err_len, "Invalid parameters");
        return ENTITY_ERR_INVALID_PARAM;
    }

    Entity existing;
    int get_res = entity_service_get_by_id(client, organization_id, entity_id, &existing, err_msg, err_len);
    if (get_res != ENTITY_ERR_OK) {
        return get_res;
    }

    char cleaned_name[151];
    int has_new_name = 0;
    if (new_name && strlen(new_name) > 0) {
        if (entity_validate_name(new_name, cleaned_name, sizeof(cleaned_name)) != 0) {
            set_error(err_msg, err_len, "Entity name must be between 1 and 150 characters");
            return ENTITY_ERR_INVALID_PARAM;
        }
        has_new_name = 1;
    }

    int has_new_type = 0;
    if (new_type != ENTITY_TYPE_UNKNOWN) {
        if (entity_validate_type(new_type) != 0) {
            set_error(err_msg, err_len, "Invalid entity type");
            return ENTITY_ERR_INVALID_PARAM;
        }
        has_new_type = 1;
    }

    if (!has_new_name && !has_new_type) {
        set_error(err_msg, err_len, "No update fields specified");
        return ENTITY_ERR_INVALID_PARAM;
    }

    char update_clause[512] = {0};
    if (has_new_name && has_new_type) {
        char esc_name[305];
        escape_string(client, cleaned_name, esc_name, sizeof(esc_name));
        const char *type_str = entity_type_to_string(new_type);
        snprintf(update_clause, sizeof(update_clause),
                 "SET name = '%s', entity_type = '%s', updated_by_user_id = %lld",
                 esc_name, type_str, user_id);
    } else if (has_new_name) {
        char esc_name[305];
        escape_string(client, cleaned_name, esc_name, sizeof(esc_name));
        snprintf(update_clause, sizeof(update_clause),
                 "SET name = '%s', updated_by_user_id = %lld",
                 esc_name, user_id);
    } else {
        const char *type_str = entity_type_to_string(new_type);
        snprintf(update_clause, sizeof(update_clause),
                 "SET entity_type = '%s', updated_by_user_id = %lld",
                 type_str, user_id);
    }

    char query[768];
    snprintf(query, sizeof(query),
             "UPDATE entities %s WHERE organization_id = %lld AND id = %lld;",
             update_clause, organization_id, entity_id);

    if (db_client_query(client, query) != 0) {
        unsigned int err_num = mysql_errno(client->connection);
        if (err_num == 1062) {
            set_error(err_msg, err_len, "Entity with this name already exists in organization");
            return ENTITY_ERR_CONFLICT;
        }
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    return entity_service_get_by_id(client, organization_id, entity_id, out_entity, err_msg, err_len);
}

int entity_service_update_status(DBClient *client, long long organization_id, long long user_id,
                                 long long entity_id, EntityStatus new_status,
                                 Entity *out_entity, char *err_msg, size_t err_len) {
    if (!client || !client->connection || organization_id <= 0 || user_id <= 0 || entity_id <= 0) {
        set_error(err_msg, err_len, "Invalid parameters");
        return ENTITY_ERR_INVALID_PARAM;
    }

    Entity existing;
    int get_res = entity_service_get_by_id(client, organization_id, entity_id, &existing, err_msg, err_len);
    if (get_res != ENTITY_ERR_OK) {
        return get_res;
    }

    if (entity_validate_status(new_status) != 0) {
        set_error(err_msg, err_len, "Invalid entity status");
        return ENTITY_ERR_INVALID_PARAM;
    }

    const char *status_str = entity_status_to_string(new_status);

    char query[512];
    snprintf(query, sizeof(query),
             "UPDATE entities SET status = '%s', updated_by_user_id = %lld "
             "WHERE organization_id = %lld AND id = %lld;",
             status_str, user_id, organization_id, entity_id);

    if (db_client_query(client, query) != 0) {
        set_error(err_msg, err_len, db_client_get_error(client));
        return ENTITY_ERR_DB;
    }

    return entity_service_get_by_id(client, organization_id, entity_id, out_entity, err_msg, err_len);
}
