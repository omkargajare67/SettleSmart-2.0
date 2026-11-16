#ifndef SESSION_H
#define SESSION_H

#include "../database/db_client.h"
#include "authorization.h"
#include <stddef.h>

typedef struct {
    long long session_id;
    long long organization_id;
    long long user_id;
    char user_email[256];
    UserRole user_role;
    char token_hash[65];
    char csrf_token_hash[65];
    char prev_csrf_token_hash[65];
} SessionInfo;

int session_create(DBClient *client, long long organization_id, long long user_id,
                   char *raw_session_token, size_t sess_len,
                   char *raw_csrf_token, size_t csrf_len);

int session_validate(DBClient *client, const char *raw_session_token, SessionInfo *out_session);

int session_validate_csrf(const SessionInfo *session, const char *raw_csrf_token);

int session_rotate_csrf_token(DBClient *client, long long session_id, char *out_csrf_token, size_t max_len);

int session_revoke(DBClient *client, const char *raw_session_token);

int session_revoke_all_user(DBClient *client, long long organization_id, long long user_id);

int session_clean_expired(DBClient *client);

#endif
