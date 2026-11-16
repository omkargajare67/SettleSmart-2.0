#include "crypto_utils.h"
#include "../../third_party/mongoose/mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

int crypto_random_bytes(unsigned char *buf, size_t len) {
    if (!buf || len == 0) {
        return -1;
    }
#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        return -1;
    }
    return 0;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    size_t total_read = 0;
    while (total_read < len) {
        ssize_t bytes_read = read(fd, buf + total_read, len - total_read);
        if (bytes_read <= 0) {
            close(fd);
            return -1;
        }
        total_read += (size_t)bytes_read;
    }
    close(fd);
    return 0;
#endif
}

void crypto_to_hex(const unsigned char *data, size_t len, char *hex_out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex_out[i * 2] = hex_chars[(data[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_chars[data[i] & 0x0F];
    }
    hex_out[len * 2] = '\0';
}

int crypto_generate_token(char *token_out, size_t max_len) {
    if (!token_out || max_len < 65) {
        return -1;
    }
    unsigned char random_data[32];
    if (crypto_random_bytes(random_data, sizeof(random_data)) != 0) {
        return -1;
    }
    crypto_to_hex(random_data, sizeof(random_data), token_out);
    return 0;
}

void crypto_sha256_hex(const char *input, size_t input_len, char *hex_out) {
    uint8_t digest[32];
    mg_sha256(digest, (uint8_t *)input, input_len);
    crypto_to_hex(digest, 32, hex_out);
}

int crypto_constant_time_equals(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a != len_b) {
        return 0;
    }
    int diff = 0;
    for (size_t i = 0; i < len_a; i++) {
        diff |= (int)((unsigned char)a[i] ^ (unsigned char)b[i]);
    }
    return diff == 0;
}
