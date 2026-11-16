#include "../src/auth/password.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("Running Password Security Tests (Group B)...\n");

    if (auth_validate_password_length("Short1!") == 0) {
        printf("FAILED: Password shorter than 12 characters was accepted\n");
        return 1;
    }
    printf("PASSED: Password shorter than 12 characters rejected\n");

    char long_pwd[135];
    memset(long_pwd, 'A', sizeof(long_pwd) - 1);
    long_pwd[sizeof(long_pwd) - 1] = '\0';
    if (auth_validate_password_length(long_pwd) == 0) {
        printf("FAILED: Password longer than 128 characters was accepted\n");
        return 1;
    }
    printf("PASSED: Password longer than 128 characters rejected\n");

    const char *pwd1 = "SecurePassword#2026";
    char hash1[AUTH_HASH_MAX_LEN];
    char hash2[AUTH_HASH_MAX_LEN];

    if (auth_hash_password(pwd1, hash1, sizeof(hash1)) != 0) {
        printf("FAILED: auth_hash_password failed for valid password\n");
        return 1;
    }
    printf("PASSED: Password hashed successfully\n");

    if (strncmp(hash1, "$argon2id$", 10) != 0) {
        printf("FAILED: Hash does not start with $argon2id$\n");
        return 1;
    }
    printf("PASSED: Hash uses standard Argon2id encoded format: %s\n", hash1);

    if (auth_hash_password(pwd1, hash2, sizeof(hash2)) != 0) {
        printf("FAILED: Second hash call failed\n");
        return 1;
    }

    if (strcmp(hash1, hash2) == 0) {
        printf("FAILED: Identical password produced identical hashes (salt uniqueness violated)\n");
        return 1;
    }
    printf("PASSED: Hashing same password twice produces unique salt and hash\n");

    if (auth_verify_password(pwd1, hash1) != 0) {
        printf("FAILED: Correct password verification failed\n");
        return 1;
    }
    printf("PASSED: Verification succeeds with correct password\n");

    if (auth_verify_password("WrongPassword#999", hash1) == 0) {
        printf("FAILED: Incorrect password verification succeeded unexpectedly\n");
        return 1;
    }
    printf("PASSED: Incorrect password verification rejected\n");

    if (auth_verify_password("securepassword#2026", hash1) == 0) {
        printf("FAILED: Case-altered password verification succeeded unexpectedly\n");
        return 1;
    }
    printf("PASSED: Case-altered password verification rejected\n");

    printf("\nALL PASSWORD SECURITY TESTS SUCCEEDED.\n");
    return 0;
}
