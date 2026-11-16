#include "transaction.h"
#include "../auth/crypto_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *transaction_status_to_string(TransactionStatus status) {
    switch (status) {
        case TRANSACTION_STATUS_PENDING:
            return "Pending";
        case TRANSACTION_STATUS_COMPLETED:
            return "Completed";
        case TRANSACTION_STATUS_REVERSED:
            return "Reversed";
        case TRANSACTION_STATUS_DISPUTED:
            return "Disputed";
        default:
            return "Unknown";
    }
}

TransactionStatus transaction_status_from_string(const char *str) {
    if (!str) {
        return TRANSACTION_STATUS_UNKNOWN;
    }
    if (strcmp(str, "Pending") == 0) {
        return TRANSACTION_STATUS_PENDING;
    }
    if (strcmp(str, "Completed") == 0) {
        return TRANSACTION_STATUS_COMPLETED;
    }
    if (strcmp(str, "Reversed") == 0) {
        return TRANSACTION_STATUS_REVERSED;
    }
    if (strcmp(str, "Disputed") == 0) {
        return TRANSACTION_STATUS_DISPUTED;
    }
    return TRANSACTION_STATUS_UNKNOWN;
}

const char *transaction_type_to_string(TransactionType type) {
    switch (type) {
        case TRANSACTION_TYPE_STANDARD:
            return "Standard";
        case TRANSACTION_TYPE_REVERSAL:
            return "Reversal";
        case TRANSACTION_TYPE_SETTLEMENT_ADJUSTMENT:
            return "SettlementAdjustment";
        default:
            return "Unknown";
    }
}

TransactionType transaction_type_from_string(const char *str) {
    if (!str) {
        return TRANSACTION_TYPE_UNKNOWN;
    }
    if (strcmp(str, "Standard") == 0) {
        return TRANSACTION_TYPE_STANDARD;
    }
    if (strcmp(str, "Reversal") == 0) {
        return TRANSACTION_TYPE_REVERSAL;
    }
    if (strcmp(str, "SettlementAdjustment") == 0 || strcmp(str, "Settlement_Adjustment") == 0) {
        return TRANSACTION_TYPE_SETTLEMENT_ADJUSTMENT;
    }
    return TRANSACTION_TYPE_UNKNOWN;
}

int transaction_parse_amount(const char *str, int64_t *out_paise, char *out_normalized, size_t out_len) {
    if (!str || !out_paise || !out_normalized || out_len < 32) {
        return -1;
    }

    const char *p = str;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        return -1;
    }

    const char *end = p + strlen(p) - 1;
    while (end > p && isspace((unsigned char)*end)) {
        end--;
    }

    size_t len = (size_t)(end - p + 1);
    if (len == 0 || len > 30) {
        return -1;
    }

    const char *dot_pos = NULL;
    int dot_count = 0;
    for (size_t i = 0; i < len; i++) {
        char ch = p[i];
        if (ch == '.') {
            dot_count++;
            dot_pos = p + i;
            if (i == 0 || i == len - 1) {
                return -1;
            }
        } else if (!isdigit((unsigned char)ch)) {
            return -1;
        }
    }

    if (dot_count > 1) {
        return -1;
    }

    size_t int_len = dot_pos ? (size_t)(dot_pos - p) : len;
    if (int_len == 0 || int_len > 13) {
        return -1;
    }

    int64_t rupees = 0;
    for (size_t i = 0; i < int_len; i++) {
        rupees = rupees * 10 + (p[i] - '0');
    }

    int64_t cents = 0;
    if (dot_pos) {
        size_t frac_len = len - int_len - 1;
        if (frac_len == 0 || frac_len > 2) {
            return -1;
        }
        if (frac_len == 1) {
            cents = (dot_pos[1] - '0') * 10;
        } else {
            cents = (dot_pos[1] - '0') * 10 + (dot_pos[2] - '0');
        }
    }

    int64_t total_paise = rupees * 100 + cents;
    if (total_paise <= 0) {
        return -1;
    }

    *out_paise = total_paise;
    snprintf(out_normalized, out_len, "%lld.%02lld", (long long)rupees, (long long)cents);
    return 0;
}

int transaction_validate_currency(const char *currency) {
    if (!currency) {
        return -1;
    }
    if (strcmp(currency, "INR") == 0) {
        return 0;
    }
    return -1;
}

int transaction_validate_idempotency_key(const char *key) {
    if (!key) {
        return -1;
    }
    size_t len = strlen(key);
    if (len != 36) {
        return -1;
    }
    for (size_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (key[i] != '-') {
                return -1;
            }
        } else {
            if (!isxdigit((unsigned char)key[i])) {
                return -1;
            }
        }
    }
    if (key[14] != '4') {
        return -1;
    }
    char v = key[19];
    if (v != '8' && v != '9' && v != 'a' && v != 'b' && v != 'A' && v != 'B') {
        return -1;
    }
    return 0;
}

int transaction_compute_fingerprint(long long org_id, long long sender_id, long long receiver_id,
                                   const char *normalized_amount, const char *currency,
                                   TransactionType type, char *out_fingerprint, size_t out_len) {
    if (!normalized_amount || !currency || !out_fingerprint || out_len < 65) {
        return -1;
    }
    const char *type_str = transaction_type_to_string(type);
    char canonical[256];
    snprintf(canonical, sizeof(canonical), "%lld:%lld:%lld:%s:%s:%s",
             org_id, sender_id, receiver_id, normalized_amount, currency, type_str);

    crypto_sha256_hex(canonical, strlen(canonical), out_fingerprint);
    return 0;
}
