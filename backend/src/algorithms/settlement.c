#include "settlement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void format_paise(int64_t paise, char *out, size_t out_len) {
    int64_t abs_val = paise < 0 ? -paise : paise;
    int64_t rupees = abs_val / 100;
    int paise_rem = (int)(abs_val % 100);
    snprintf(out, out_len, "%lld.%02d", (long long)rupees, paise_rem);
}

int settlement_plan_init(SettlementPlan *plan, size_t initial_capacity) {
    if (!plan) {
        return -1;
    }

    size_t cap = initial_capacity < 8 ? 8 : initial_capacity;
    plan->recommendations = (SettlementRecommendation *)malloc(cap * sizeof(SettlementRecommendation));
    if (!plan->recommendations) {
        return -1;
    }

    plan->count = 0;
    plan->capacity = cap;
    plan->total_settled_paise = 0;
    return 0;
}

void settlement_plan_destroy(SettlementPlan *plan) {
    if (!plan) {
        return;
    }

    if (plan->recommendations) {
        free(plan->recommendations);
        plan->recommendations = NULL;
    }
    plan->count = 0;
    plan->capacity = 0;
    plan->total_settled_paise = 0;
}

static int plan_add_recommendation(SettlementPlan *plan, long long from_id, long long to_id, int64_t amount) {
    if (!plan || amount <= 0) {
        return -1;
    }

    if (plan->count >= plan->capacity) {
        size_t new_cap = plan->capacity * 2;
        SettlementRecommendation *new_arr = (SettlementRecommendation *)realloc(plan->recommendations, new_cap * sizeof(SettlementRecommendation));
        if (!new_arr) {
            return -1;
        }
        plan->recommendations = new_arr;
        plan->capacity = new_cap;
    }

    SettlementRecommendation *rec = &plan->recommendations[plan->count];
    rec->from_entity_id = from_id;
    rec->to_entity_id = to_id;
    rec->amount_paise = amount;
    format_paise(amount, rec->amount_str, sizeof(rec->amount_str));

    plan->count++;
    plan->total_settled_paise += amount;
    return 0;
}

typedef struct {
    long long entity_id;
    int64_t amount;
} Participant;

static void sort_participants(Participant *arr, size_t n) {
    if (!arr || n < 2) {
        return;
    }

    for (size_t i = 0; i < n - 1; i++) {
        int swapped = 0;
        for (size_t j = 0; j < n - 1 - i; j++) {
            int need_swap = 0;
            if (arr[j].amount < arr[j + 1].amount) {
                need_swap = 1;
            } else if (arr[j].amount == arr[j + 1].amount) {
                if (arr[j].entity_id > arr[j + 1].entity_id) {
                    need_swap = 1;
                }
            }

            if (need_swap) {
                Participant tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
                swapped = 1;
            }
        }
        if (!swapped) {
            break;
        }
    }
}

int settlement_compute_recommendations(const EntityBalance *balances, size_t balance_count,
                                      SettlementPlan *out_plan, char *err_msg, size_t err_len) {
    if (err_msg && err_len > 0) {
        err_msg[0] = '\0';
    }

    if (!out_plan) {
        if (err_msg && err_len > 0) {
            snprintf(err_msg, err_len, "Invalid settlement plan output pointer");
        }
        return SETTLEMENT_ERR_INVALID_PARAM;
    }

    if (!balances || balance_count == 0) {
        return SETTLEMENT_OK;
    }

    int64_t total_sum = 0;
    size_t debtor_count = 0;
    size_t creditor_count = 0;

    for (size_t i = 0; i < balance_count; i++) {
        total_sum += balances[i].balance_paise;
        if (balances[i].balance_paise < 0) {
            debtor_count++;
        } else if (balances[i].balance_paise > 0) {
            creditor_count++;
        }
    }

    if (total_sum != 0) {
        if (err_msg && err_len > 0) {
            snprintf(err_msg, err_len, "Entity balances are not balanced: total net sum is %lld paise (must be 0)", (long long)total_sum);
        }
        return SETTLEMENT_ERR_UNBALANCED;
    }

    if (debtor_count == 0 || creditor_count == 0) {
        return SETTLEMENT_OK;
    }

    Participant *debtors = (Participant *)malloc(debtor_count * sizeof(Participant));
    Participant *creditors = (Participant *)malloc(creditor_count * sizeof(Participant));

    if (!debtors || !creditors) {
        if (debtors) free(debtors);
        if (creditors) free(creditors);
        if (err_msg && err_len > 0) {
            snprintf(err_msg, err_len, "Memory allocation failed for settlement calculation");
        }
        return SETTLEMENT_ERR_NO_MEMORY;
    }

    size_t d_idx = 0;
    size_t c_idx = 0;

    for (size_t i = 0; i < balance_count; i++) {
        if (balances[i].balance_paise < 0) {
            debtors[d_idx].entity_id = balances[i].entity_id;
            debtors[d_idx].amount = -balances[i].balance_paise;
            d_idx++;
        } else if (balances[i].balance_paise > 0) {
            creditors[c_idx].entity_id = balances[i].entity_id;
            creditors[c_idx].amount = balances[i].balance_paise;
            c_idx++;
        }
    }

    sort_participants(debtors, debtor_count);
    sort_participants(creditors, creditor_count);

    size_t d_curr = 0;
    size_t c_curr = 0;

    while (d_curr < debtor_count && c_curr < creditor_count) {
        int64_t settle_amt = debtors[d_curr].amount < creditors[c_curr].amount ?
                             debtors[d_curr].amount : creditors[c_curr].amount;

        if (settle_amt > 0) {
            if (plan_add_recommendation(out_plan, debtors[d_curr].entity_id, creditors[c_curr].entity_id, settle_amt) != 0) {
                free(debtors);
                free(creditors);
                if (err_msg && err_len > 0) {
                    snprintf(err_msg, err_len, "Failed to record settlement recommendation");
                }
                return SETTLEMENT_ERR_NO_MEMORY;
            }

            debtors[d_curr].amount -= settle_amt;
            creditors[c_curr].amount -= settle_amt;
        }

        if (debtors[d_curr].amount == 0) {
            d_curr++;
        }
        if (creditors[c_curr].amount == 0) {
            c_curr++;
        }
    }

    free(debtors);
    free(creditors);
    return SETTLEMENT_OK;
}
