#ifndef SETTLEMENT_H
#define SETTLEMENT_H

#include "binary_search.h"
#include <stddef.h>
#include <stdint.h>

#define SETTLEMENT_OK 0
#define SETTLEMENT_ERR_INVALID_PARAM -1
#define SETTLEMENT_ERR_UNBALANCED -2
#define SETTLEMENT_ERR_NO_MEMORY -3

typedef struct {
    long long from_entity_id;
    long long to_entity_id;
    int64_t amount_paise;
    char amount_str[32];
} SettlementRecommendation;

typedef struct {
    SettlementRecommendation *recommendations;
    size_t count;
    size_t capacity;
    int64_t total_settled_paise;
} SettlementPlan;

int settlement_plan_init(SettlementPlan *plan, size_t initial_capacity);
void settlement_plan_destroy(SettlementPlan *plan);
int settlement_compute_recommendations(const EntityBalance *balances, size_t balance_count,
                                      SettlementPlan *out_plan, char *err_msg, size_t err_len);

#endif
