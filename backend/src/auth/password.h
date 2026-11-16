#ifndef PASSWORD_H
#define PASSWORD_H

#include <stddef.h>

#define AUTH_PASSWORD_MIN_LEN 12
#define AUTH_PASSWORD_MAX_LEN 128
#define AUTH_HASH_MAX_LEN 256

int auth_validate_password_length(const char *password);
int auth_hash_password(const char *password, char *hash_out, size_t hash_out_len);
int auth_verify_password(const char *password, const char *stored_hash);

#endif
