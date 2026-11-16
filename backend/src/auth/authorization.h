#ifndef AUTHORIZATION_H
#define AUTHORIZATION_H

#include "../database/db_client.h"

typedef enum {
    ROLE_UNKNOWN = 0,
    ROLE_ADMIN = 1,
    ROLE_FINANCE_MANAGER = 2,
    ROLE_VIEWER = 3
} UserRole;

UserRole auth_role_from_string(const char *role_str);
const char *auth_role_to_string(UserRole role);
int auth_can_manage_users(UserRole role);
int auth_can_create_transactions(UserRole role);
int auth_can_view_data(UserRole role);
int auth_check_is_last_active_admin(DBClient *client, long long organization_id, long long user_id);

#endif
