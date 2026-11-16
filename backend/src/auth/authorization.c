#include "authorization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UserRole auth_role_from_string(const char *role_str) {
    if (!role_str) {
        return ROLE_UNKNOWN;
    }
    if (strcmp(role_str, "Admin") == 0) {
        return ROLE_ADMIN;
    }
    if (strcmp(role_str, "Finance_Manager") == 0) {
        return ROLE_FINANCE_MANAGER;
    }
    if (strcmp(role_str, "Viewer") == 0) {
        return ROLE_VIEWER;
    }
    return ROLE_UNKNOWN;
}

const char *auth_role_to_string(UserRole role) {
    switch (role) {
        case ROLE_ADMIN:
            return "Admin";
        case ROLE_FINANCE_MANAGER:
            return "Finance_Manager";
        case ROLE_VIEWER:
            return "Viewer";
        default:
            return "Unknown";
    }
}

int auth_can_manage_users(UserRole role) {
    return (role == ROLE_ADMIN) ? 1 : 0;
}

int auth_can_create_transactions(UserRole role) {
    return (role == ROLE_ADMIN || role == ROLE_FINANCE_MANAGER) ? 1 : 0;
}

int auth_can_view_data(UserRole role) {
    return (role == ROLE_ADMIN || role == ROLE_FINANCE_MANAGER || role == ROLE_VIEWER) ? 1 : 0;
}

int auth_check_is_last_active_admin(DBClient *client, long long organization_id, long long user_id) {
    if (!client) {
        return -1;
    }

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT COUNT(*) FROM users WHERE organization_id = %lld AND role = 'Admin' AND status = 'Active' AND id <> %lld;",
             organization_id, user_id);

    if (db_client_query(client, query) != 0) {
        return -1;
    }

    MYSQL_RES *res = db_client_store_result(client);
    if (!res) {
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    int other_active_admins = 0;
    if (row && row[0]) {
        other_active_admins = atoi(row[0]);
    }
    db_client_free_result(res);

    if (other_active_admins == 0) {
        return 1;
    }
    return 0;
}
