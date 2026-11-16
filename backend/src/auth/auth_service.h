#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include "../database/db_client.h"
#include "authorization.h"
#include <stddef.h>

typedef struct {
    long long id;
    long long organization_id;
    char email[256];
    UserRole role;
    char status[32];
} AuthUserInfo;

int auth_register_organization(DBClient *client, const char *org_name,
                               const char *admin_email, const char *admin_password,
                               long long *out_org_id, long long *out_user_id,
                               char *err_msg, size_t err_len);

int auth_login(DBClient *client, long long organization_id,
               const char *email, const char *password,
               char *out_session_token, size_t sess_len,
               char *out_csrf_token, size_t csrf_len,
               AuthUserInfo *out_user,
               char *err_msg, size_t err_len);

int auth_change_password(DBClient *client, long long organization_id, long long user_id,
                         const char *old_password, const char *new_password,
                         char *err_msg, size_t err_len);

int auth_create_user(DBClient *client, long long actor_user_id, UserRole actor_role,
                     long long organization_id, const char *email, const char *password,
                     UserRole role, long long *out_user_id,
                     char *err_msg, size_t err_len);

int auth_update_user_status(DBClient *client, long long actor_user_id, UserRole actor_role,
                            long long organization_id, long long target_user_id,
                            const char *new_status,
                            char *err_msg, size_t err_len);

int auth_update_user_role(DBClient *client, long long actor_user_id, UserRole actor_role,
                          long long organization_id, long long target_user_id,
                          UserRole new_role,
                          char *err_msg, size_t err_len);

int auth_get_user_by_id(DBClient *client, long long organization_id, long long user_id,
                        AuthUserInfo *out_user);

void auth_record_failed_attempt(long long organization_id, const char *email);
void auth_reset_failed_attempts(long long organization_id, const char *email);
int auth_is_rate_limited(long long organization_id, const char *email);

void auth_record_failed_attempt_db(DBClient *client, long long organization_id, const char *email);
void auth_reset_failed_attempts_db(DBClient *client, long long organization_id, const char *email);
int auth_is_rate_limited_db(DBClient *client, long long organization_id, const char *email);

#endif
