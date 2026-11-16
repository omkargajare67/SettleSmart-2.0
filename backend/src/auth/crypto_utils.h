#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <stddef.h>

int crypto_random_bytes(unsigned char *buf, size_t len);
void crypto_to_hex(const unsigned char *data, size_t len, char *hex_out);
int crypto_generate_token(char *token_out, size_t max_len);
void crypto_sha256_hex(const char *input, size_t input_len, char *hex_out);
int crypto_constant_time_equals(const char *a, const char *b);

#endif
