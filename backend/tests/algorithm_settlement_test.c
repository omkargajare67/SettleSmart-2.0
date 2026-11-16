#include "../src/algorithms/settlement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define EXPECT_TRUE(cond, desc) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", desc); \
    } else { \
        printf("  [FAIL] %s (line %d)\n", desc, __LINE__); \
    } \
} while(0)

int main(void) {
    printf("=== RUNNING GREEDY SETTLEMENT ALGORITHM TESTS ===\n");

    {
        EntityBalance b[2];
        b[0].entity_id = 101; b[0].balance_paise = -5000;
        b[1].entity_id = 102; b[1].balance_paise = 5000;

        SettlementPlan plan;
        settlement_plan_init(&plan, 4);

        char err[128];
        int rc = settlement_compute_recommendations(b, 2, &plan, err, sizeof(err));

        EXPECT_TRUE(rc == SETTLEMENT_OK, "Compute bilateral settlement success");
        EXPECT_TRUE(plan.count == 1, "Bilateral settlement produces exactly 1 recommendation");
        if (plan.count == 1) {
            EXPECT_TRUE(plan.recommendations[0].from_entity_id == 101, "Debtor is payer");
            EXPECT_TRUE(plan.recommendations[0].to_entity_id == 102, "Creditor is payee");
            EXPECT_TRUE(plan.recommendations[0].amount_paise == 5000, "Amount is 5000 paise");
            EXPECT_TRUE(strcmp(plan.recommendations[0].amount_str, "50.00") == 0, "Formatted string is 50.00");
        }
        EXPECT_TRUE(plan.total_settled_paise == 5000, "Total settled paise is 5000");

        settlement_plan_destroy(&plan);
    }

    {
        EntityBalance b[4];
        b[0].entity_id = 1; b[0].balance_paise = -7000;
        b[1].entity_id = 2; b[1].balance_paise = -3000;
        b[2].entity_id = 3; b[2].balance_paise = 6000;
        b[3].entity_id = 4; b[3].balance_paise = 4000;

        SettlementPlan plan;
        settlement_plan_init(&plan, 8);

        char err[128];
        int rc = settlement_compute_recommendations(b, 4, &plan, err, sizeof(err));

        EXPECT_TRUE(rc == SETTLEMENT_OK, "Compute multi-party settlement success");
        EXPECT_TRUE(plan.count == 3, "Multi-party produces 3 recommendations");
        EXPECT_TRUE(plan.total_settled_paise == 10000, "Total settled paise is 10000");

        if (plan.count == 3) {
            EXPECT_TRUE(plan.recommendations[0].from_entity_id == 1 &&
                        plan.recommendations[0].to_entity_id == 3 &&
                        plan.recommendations[0].amount_paise == 6000,
                        "First match is largest debtor (1) to largest creditor (3) for 6000");

            EXPECT_TRUE(plan.recommendations[1].from_entity_id == 1 &&
                        plan.recommendations[1].to_entity_id == 4 &&
                        plan.recommendations[1].amount_paise == 1000,
                        "Second match is remainder of debtor (1) to creditor (4) for 1000");

            EXPECT_TRUE(plan.recommendations[2].from_entity_id == 2 &&
                        plan.recommendations[2].to_entity_id == 4 &&
                        plan.recommendations[2].amount_paise == 3000,
                        "Third match is debtor (2) to remainder of creditor (4) for 3000");
        }

        settlement_plan_destroy(&plan);
    }

    {
        EntityBalance b[3];
        b[0].entity_id = 10; b[0].balance_paise = 0;
        b[1].entity_id = 20; b[1].balance_paise = 0;
        b[2].entity_id = 30; b[2].balance_paise = 0;

        SettlementPlan plan;
        settlement_plan_init(&plan, 4);

        char err[128];
        int rc = settlement_compute_recommendations(b, 3, &plan, err, sizeof(err));

        EXPECT_TRUE(rc == SETTLEMENT_OK, "Zero balances compute successfully");
        EXPECT_TRUE(plan.count == 0, "No recommendations for zero balances");
        EXPECT_TRUE(plan.total_settled_paise == 0, "Total settled paise is zero");

        settlement_plan_destroy(&plan);
    }

    {
        EntityBalance b[2];
        b[0].entity_id = 1; b[0].balance_paise = -5000;
        b[1].entity_id = 2; b[1].balance_paise = 4000;

        SettlementPlan plan;
        settlement_plan_init(&plan, 4);

        char err[128];
        int rc = settlement_compute_recommendations(b, 2, &plan, err, sizeof(err));

        EXPECT_TRUE(rc == SETTLEMENT_ERR_UNBALANCED, "Unbalanced system returns SETTLEMENT_ERR_UNBALANCED");
        EXPECT_TRUE(strlen(err) > 0, "Unbalanced system provides descriptive error message");

        settlement_plan_destroy(&plan);
    }

    {
        EntityBalance b[4];
        b[0].entity_id = 20; b[0].balance_paise = -2500;
        b[1].entity_id = 10; b[1].balance_paise = -2500;
        b[2].entity_id = 40; b[2].balance_paise = 2500;
        b[3].entity_id = 30; b[3].balance_paise = 2500;

        SettlementPlan plan;
        settlement_plan_init(&plan, 4);

        char err[128];
        int rc = settlement_compute_recommendations(b, 4, &plan, err, sizeof(err));

        EXPECT_TRUE(rc == SETTLEMENT_OK, "Deterministic tie-breaking computation succeeds");
        EXPECT_TRUE(plan.count == 2, "Produces 2 recommendations");
        if (plan.count == 2) {
            EXPECT_TRUE(plan.recommendations[0].from_entity_id == 10 && plan.recommendations[0].to_entity_id == 30,
                        "Lower entity_id 10 pays lower creditor entity_id 30");
            EXPECT_TRUE(plan.recommendations[1].from_entity_id == 20 && plan.recommendations[1].to_entity_id == 40,
                        "Higher entity_id 20 pays higher creditor entity_id 40");
        }

        settlement_plan_destroy(&plan);
    }

    printf("\nSettlement Algorithm Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
