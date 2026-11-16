#include "../src/transactions/transaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int g_tests_passed = 0;

static void test_amount_parsing(void) {
    int64_t paise = 0;
    char norm[32];

    assert(transaction_parse_amount("1500.50", &paise, norm, sizeof(norm)) == 0);
    assert(paise == 150050);
    assert(strcmp(norm, "1500.50") == 0);
    g_tests_passed++;

    assert(transaction_parse_amount("100", &paise, norm, sizeof(norm)) == 0);
    assert(paise == 10000);
    assert(strcmp(norm, "100.00") == 0);
    g_tests_passed++;

    assert(transaction_parse_amount("0.05", &paise, norm, sizeof(norm)) == 0);
    assert(paise == 5);
    assert(strcmp(norm, "0.05") == 0);
    g_tests_passed++;

    assert(transaction_parse_amount("0.5", &paise, norm, sizeof(norm)) == 0);
    assert(paise == 50);
    assert(strcmp(norm, "0.50") == 0);
    g_tests_passed++;

    assert(transaction_parse_amount("9999999999999.99", &paise, norm, sizeof(norm)) == 0);
    assert(paise == 999999999999999LL);
    assert(strcmp(norm, "9999999999999.99") == 0);
    g_tests_passed++;

    assert(transaction_parse_amount("0", &paise, norm, sizeof(norm)) != 0);
    assert(transaction_parse_amount("0.00", &paise, norm, sizeof(norm)) != 0);
    assert(transaction_parse_amount("-10.00", &paise, norm, sizeof(norm)) != 0);
    assert(transaction_parse_amount("12.345", &paise, norm, sizeof(norm)) != 0);
    assert(transaction_parse_amount("abc", &paise, norm, sizeof(norm)) != 0);
    assert(transaction_parse_amount("", &paise, norm, sizeof(norm)) != 0);
    assert(transaction_parse_amount(NULL, &paise, norm, sizeof(norm)) != 0);
    assert(transaction_parse_amount("10000000000000.00", &paise, norm, sizeof(norm)) != 0);
    g_tests_passed++;
}

static void test_currency_validation(void) {
    assert(transaction_validate_currency("INR") == 0);
    assert(transaction_validate_currency("inr") != 0);
    assert(transaction_validate_currency("USD") != 0);
    assert(transaction_validate_currency("EUR") != 0);
    assert(transaction_validate_currency("") != 0);
    assert(transaction_validate_currency(NULL) != 0);
    g_tests_passed++;
}

static void test_uuid_v4_validation(void) {
    assert(transaction_validate_idempotency_key("a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11") == 0);
    assert(transaction_validate_idempotency_key("A0EEBC99-9C0B-4EF8-BB6D-6BB9BD380A11") == 0);
    assert(transaction_validate_idempotency_key("00000000-0000-4000-8000-000000000000") == 0);
    assert(transaction_validate_idempotency_key("00000000-0000-4000-9000-000000000000") == 0);
    assert(transaction_validate_idempotency_key("00000000-0000-4000-a000-000000000000") == 0);
    assert(transaction_validate_idempotency_key("00000000-0000-4000-b000-000000000000") == 0);

    assert(transaction_validate_idempotency_key("a0eebc99-9c0b-3ef8-bb6d-6bb9bd380a11") != 0);
    assert(transaction_validate_idempotency_key("a0eebc99-9c0b-4ef8-cb6d-6bb9bd380a11") != 0);
    assert(transaction_validate_idempotency_key("a0eebc99-9c0b-4ef8-7b6d-6bb9bd380a11") != 0);
    assert(transaction_validate_idempotency_key("a0eebc999c0b4ef8bb6d6bb9bd380a11") != 0);
    assert(transaction_validate_idempotency_key("invalid-uuid") != 0);
    assert(transaction_validate_idempotency_key("") != 0);
    assert(transaction_validate_idempotency_key(NULL) != 0);
    g_tests_passed++;
}

static void test_status_and_type_conversions(void) {
    assert(strcmp(transaction_status_to_string(TRANSACTION_STATUS_PENDING), "Pending") == 0);
    assert(strcmp(transaction_status_to_string(TRANSACTION_STATUS_COMPLETED), "Completed") == 0);
    assert(strcmp(transaction_status_to_string(TRANSACTION_STATUS_REVERSED), "Reversed") == 0);
    assert(strcmp(transaction_status_to_string(TRANSACTION_STATUS_DISPUTED), "Disputed") == 0);
    assert(transaction_status_from_string("Pending") == TRANSACTION_STATUS_PENDING);
    assert(transaction_status_from_string("Completed") == TRANSACTION_STATUS_COMPLETED);
    assert(transaction_status_from_string("Reversed") == TRANSACTION_STATUS_REVERSED);
    assert(transaction_status_from_string("Disputed") == TRANSACTION_STATUS_DISPUTED);
    assert(transaction_status_from_string("unknown") == TRANSACTION_STATUS_UNKNOWN);

    assert(strcmp(transaction_type_to_string(TRANSACTION_TYPE_STANDARD), "Standard") == 0);
    assert(strcmp(transaction_type_to_string(TRANSACTION_TYPE_REVERSAL), "Reversal") == 0);
    assert(strcmp(transaction_type_to_string(TRANSACTION_TYPE_SETTLEMENT_ADJUSTMENT), "SettlementAdjustment") == 0);
    assert(transaction_type_from_string("Standard") == TRANSACTION_TYPE_STANDARD);
    assert(transaction_type_from_string("Reversal") == TRANSACTION_TYPE_REVERSAL);
    assert(transaction_type_from_string("SettlementAdjustment") == TRANSACTION_TYPE_SETTLEMENT_ADJUSTMENT);
    assert(transaction_type_from_string("unknown") == TRANSACTION_TYPE_UNKNOWN);
    g_tests_passed++;
}

static void test_fingerprint_generation(void) {
    char fp1[65];
    char fp2[65];
    char fp3[65];

    assert(transaction_compute_fingerprint(1, 10, 20, "1500.50", "INR", TRANSACTION_TYPE_STANDARD, fp1, sizeof(fp1)) == 0);
    assert(transaction_compute_fingerprint(1, 10, 20, "1500.50", "INR", TRANSACTION_TYPE_STANDARD, fp2, sizeof(fp2)) == 0);
    assert(strcmp(fp1, fp2) == 0);

    assert(transaction_compute_fingerprint(1, 10, 20, "1500.51", "INR", TRANSACTION_TYPE_STANDARD, fp3, sizeof(fp3)) == 0);
    assert(strcmp(fp1, fp3) != 0);
    g_tests_passed++;
}

int main(void) {
    test_amount_parsing();
    test_currency_validation();
    test_uuid_v4_validation();
    test_status_and_type_conversions();
    test_fingerprint_generation();

    printf("ALL TRANSACTION VALIDATION TESTS PASSED (%d checks)\n", g_tests_passed);
    return 0;
}
