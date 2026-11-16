#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    TRANSACTION_STATUS_UNKNOWN = 0,
    TRANSACTION_STATUS_PENDING,
    TRANSACTION_STATUS_COMPLETED,
    TRANSACTION_STATUS_REVERSED,
    TRANSACTION_STATUS_DISPUTED
} TransactionStatus;

typedef enum {
    TRANSACTION_TYPE_UNKNOWN = 0,
    TRANSACTION_TYPE_STANDARD,
    TRANSACTION_TYPE_REVERSAL,
    TRANSACTION_TYPE_SETTLEMENT_ADJUSTMENT
} TransactionType;

typedef struct {
    long long id;
    long long organization_id;
    long long sender_entity_id;
    long long receiver_entity_id;
    int64_t amount_paise;
    char amount_str[32];
    char currency[4];
    TransactionStatus status;
    TransactionType transaction_type;
    char idempotency_key[65];
    char request_fingerprint[65];
    long long reversal_of_id;
    long long created_by_user_id;
    char description[256];
    char created_at[32];
} Transaction;

const char *transaction_status_to_string(TransactionStatus status);
TransactionStatus transaction_status_from_string(const char *str);
const char *transaction_type_to_string(TransactionType type);
TransactionType transaction_type_from_string(const char *str);

int transaction_parse_amount(const char *str, int64_t *out_paise, char *out_normalized, size_t out_len);
int transaction_validate_currency(const char *currency);
int transaction_validate_idempotency_key(const char *key);
int transaction_compute_fingerprint(long long org_id, long long sender_id, long long receiver_id,
                                   const char *normalized_amount, const char *currency,
                                   TransactionType type, char *out_fingerprint, size_t out_len);

#endif
