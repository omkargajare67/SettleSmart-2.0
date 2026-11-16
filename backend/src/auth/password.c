#include "password.h"
#include "crypto_utils.h"
#include "../../third_party/argon2/include/argon2.h"
#include <string.h>

#define ARGON2_TIME_COST 2
#define ARGON2_MEMORY_COST 65536
#define ARGON2_PARALLELISM 1
#define ARGON2_SALT_LEN 32
#define ARGON2_HASH_LEN 32

int auth_validate_password_length(const char *password) {
    if (!password) {
        return -1;
    }
    size_t len = strlen(password);
    if (len < AUTH_PASSWORD_MIN_LEN) {
        return -1;
    }
    if (len > AUTH_PASSWORD_MAX_LEN) {
        return -2;
    }
    return 0;
}

int auth_hash_password(const char *password, char *hash_out, size_t hash_out_len) {
    if (auth_validate_password_length(password) != 0) {
        return -1;
    }
    if (!hash_out || hash_out_len < 128) {
        return -2;
    }

    unsigned char salt[ARGON2_SALT_LEN];
    if (crypto_random_bytes(salt, sizeof(salt)) != 0) {
        return -3;
    }

    size_t pwd_len = strlen(password);
    int res = argon2id_hash_encoded(
        ARGON2_TIME_COST,
        ARGON2_MEMORY_COST,
        ARGON2_PARALLELISM,
        password,
        pwd_len,
        salt,
        sizeof(salt),
        ARGON2_HASH_LEN,
        hash_out,
        hash_out_len
    );

    if (res != ARGON2_OK) {
        return -4;
    }

    return 0;
}

int auth_verify_password(const char *password, const char *stored_hash) {
    if (!password || !stored_hash) {
        return -1;
    }
    size_t pwd_len = strlen(password);
    int res = argon2id_verify(stored_hash, password, pwd_len);
    if (res != ARGON2_OK) {
        return -1;
    }
    return 0;
}
