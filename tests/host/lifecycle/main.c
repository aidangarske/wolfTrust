/* main.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 *
 * wolfTrust is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfTrust is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* WT-SYS-0008 / WT-FFM-0017: a restartable partition fault stays within the
 * declared restart budget and window; exhausting the budget fails closed. This
 * exercises the production restart-budget engine (wt_restart_policy_evaluate)
 * that src/monitor.c drives on every guest fault. */

#include "wolftrust/restart_policy.h"

#include <stdio.h>

static unsigned int g_checks;
static unsigned int g_failures;

#define EXPECT_DECISION(actual, expected) \
    do { \
        wt_restart_decision_t actual_decision = (actual); \
        wt_restart_decision_t expected_decision = (expected); \
        g_checks++; \
        if (actual_decision != expected_decision) { \
            (void)fprintf(stderr, "line %d: expected decision %d, received %d\n", \
                          __LINE__, (int)expected_decision, (int)actual_decision); \
            g_failures++; \
        } \
    } while (0)

#define EXPECT_U32(actual, expected) \
    do { \
        uint32_t actual_value = (actual); \
        uint32_t expected_value = (expected); \
        g_checks++; \
        if (actual_value != expected_value) { \
            (void)fprintf(stderr, "line %d: expected %u, received %u\n", \
                          __LINE__, expected_value, actual_value); \
            g_failures++; \
        } \
    } while (0)

/* Budget of 2 over a 10-tick window: two restarts allowed, the third within the
 * window faults, and a full crash-free window clears the budget. */
static void wt_test_budget_and_window(void)
{
    uint32_t count = 0U;
    uint32_t first = 0U;

    EXPECT_DECISION(wt_restart_policy_evaluate(2U, 10U, 0U, &count, &first),
                    WT_RESTART_DECISION_RESTART);
    EXPECT_U32(count, 1U);
    EXPECT_U32(first, 0U);

    EXPECT_DECISION(wt_restart_policy_evaluate(2U, 10U, 5U, &count, &first),
                    WT_RESTART_DECISION_RESTART);
    EXPECT_U32(count, 2U);
    EXPECT_U32(first, 5U);

    /* Budget exhausted within the window: fail closed, counters unchanged. */
    EXPECT_DECISION(wt_restart_policy_evaluate(2U, 10U, 8U, &count, &first),
                    WT_RESTART_DECISION_FAULT);
    EXPECT_U32(count, 2U);
    EXPECT_U32(first, 5U);

    /* A full crash-free window since the last restart (tick 5) clears the
     * budget, so the next fault restarts again. */
    EXPECT_DECISION(wt_restart_policy_evaluate(2U, 10U, 20U, &count, &first),
                    WT_RESTART_DECISION_RESTART);
    EXPECT_U32(count, 1U);
    EXPECT_U32(first, 20U);
}

/* Wall-time alone must not clear a crash loop: with a zero window the budget
 * never resets, so once exhausted the domain stays faulted. */
static void wt_test_zero_window(void)
{
    uint32_t count = 0U;
    uint32_t first = 0U;

    EXPECT_DECISION(wt_restart_policy_evaluate(1U, 0U, 0U, &count, &first),
                    WT_RESTART_DECISION_RESTART);
    EXPECT_U32(count, 1U);
    EXPECT_DECISION(wt_restart_policy_evaluate(1U, 0U, 1000U, &count, &first),
                    WT_RESTART_DECISION_FAULT);
    EXPECT_U32(count, 1U);
}

/* A zero restart limit means unlimited restarts. */
static void wt_test_unlimited(void)
{
    uint32_t count = 0U;
    uint32_t first = 0U;
    unsigned int i;

    for (i = 0U; i < 5U; ++i) {
        EXPECT_DECISION(wt_restart_policy_evaluate(0U, 10U, i, &count, &first),
                        WT_RESTART_DECISION_RESTART);
    }
    EXPECT_U32(count, 5U);
    EXPECT_U32(first, 4U);
}

static void wt_test_arguments(void)
{
    uint32_t count = 0U;
    uint32_t first = 0U;

    EXPECT_DECISION(wt_restart_policy_evaluate(2U, 10U, 0U, NULL, &first),
                    WT_RESTART_DECISION_FAULT);
    EXPECT_DECISION(wt_restart_policy_evaluate(2U, 10U, 0U, &count, NULL),
                    WT_RESTART_DECISION_FAULT);
}

int main(void)
{
    wt_test_budget_and_window();
    wt_test_zero_window();
    wt_test_unlimited();
    wt_test_arguments();
    if (g_failures != 0U) {
        (void)fprintf(stderr, "restart-policy checks failed: %u/%u\n",
                      g_failures, g_checks);
        return 1;
    }
    (void)printf("WT-SYS-0008 restart-policy checks passed: %u\n", g_checks);
    return 0;
}
