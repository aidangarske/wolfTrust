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

#include "wolftrust/lifecycle.h"

#include <stdio.h>
#include <string.h>

static unsigned int g_checks;
static unsigned int g_failures;

#define EXPECT_RESULT(actual, expected) \
    do { \
        int actual_result = (actual); \
        int expected_result = (expected); \
        g_checks++; \
        if (actual_result != expected_result) { \
            (void)fprintf(stderr, "line %d: expected %d, received %d\n", \
                          __LINE__, expected_result, actual_result); \
            g_failures++; \
        } \
    } while (0)

#define EXPECT_STATE(runtime, expected) \
    do { \
        g_checks++; \
        if ((runtime).state != (expected)) { \
            (void)fprintf(stderr, "line %d: unexpected lifecycle state\n", \
                          __LINE__); \
            g_failures++; \
        } \
    } while (0)

static wt_lifecycle_policy_t wt_domain_policy(void)
{
    wt_lifecycle_policy_t policy;

    (void)memset(&policy, 0, sizeof(policy));
    policy.action = WT_RESTART_ACTION_DOMAIN;
    policy.restart_limit = 2U;
    policy.restart_window_ticks = 10U;
    return policy;
}

static void wt_test_lifecycle_flow(void)
{
    wt_lifecycle_runtime_t runtime = {
        WT_DOMAIN_LIFECYCLE_STOPPED, 0U, 0U
    };
    wt_lifecycle_policy_t policy = wt_domain_policy();

    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_START, 0U),
                  WT_LIFECYCLE_ERROR_TRANSITION);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_INITIALIZE, 0U),
                  WT_LIFECYCLE_VALID);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_READY);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_START, 1U),
                  WT_LIFECYCLE_VALID);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_RUNNING);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_WAIT, 2U),
                  WT_LIFECYCLE_VALID);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_BLOCKED);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_SIGNAL, 3U),
                  WT_LIFECYCLE_VALID);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_READY);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_PREEMPT, 4U),
                  WT_LIFECYCLE_ERROR_TRANSITION);
}

static void wt_test_restart_containment(void)
{
    wt_lifecycle_runtime_t runtime = {
        WT_DOMAIN_LIFECYCLE_RUNNING, 0U, 0U
    };
    wt_lifecycle_policy_t policy = wt_domain_policy();

    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_PANIC, 10U),
                  WT_LIFECYCLE_VALID);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_RESTARTING);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_RESTART, 11U),
                  WT_LIFECYCLE_VALID);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_READY);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_START, 12U),
                  WT_LIFECYCLE_VALID);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_PANIC, 13U),
                  WT_LIFECYCLE_VALID);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_RESTART, 14U),
                  WT_LIFECYCLE_VALID);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_START, 15U),
                  WT_LIFECYCLE_VALID);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_PANIC, 16U),
                  WT_LIFECYCLE_ERROR_TERMINAL);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_FAULTED);
}

static void wt_test_terminal_policy(void)
{
    wt_lifecycle_runtime_t runtime = {
        WT_DOMAIN_LIFECYCLE_RUNNING, 0U, 0U
    };
    wt_lifecycle_policy_t policy = {
        WT_RESTART_ACTION_NEVER, 0U, 0U
    };

    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_PANIC, 0U),
                  WT_LIFECYCLE_ERROR_TERMINAL);
    EXPECT_STATE(runtime, WT_DOMAIN_LIFECYCLE_FAULTED);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, &policy,
                                          WT_LIFECYCLE_EVENT_START, 1U),
                  WT_LIFECYCLE_ERROR_TRANSITION);
}

static void wt_test_selector(void)
{
    wt_lifecycle_slot_t slots[4] = {
        { WT_DOMAIN_LIFECYCLE_READY, 1U },
        { WT_DOMAIN_LIFECYCLE_READY, 2U },
        { WT_DOMAIN_LIFECYCLE_READY, 2U },
        { WT_DOMAIN_LIFECYCLE_BLOCKED, 3U }
    };
    size_t selected = 0U;

    EXPECT_RESULT(wt_lifecycle_select_next(slots, 4U, 1U, &selected),
                  WT_LIFECYCLE_VALID);
    EXPECT_RESULT((int)selected, 1);
    slots[1].state = WT_DOMAIN_LIFECYCLE_RUNNING;
    EXPECT_RESULT(wt_lifecycle_select_next(slots, 4U, 1U, &selected),
                  WT_LIFECYCLE_VALID);
    EXPECT_RESULT((int)selected, 2);
    slots[0].state = WT_DOMAIN_LIFECYCLE_BLOCKED;
    slots[2].state = WT_DOMAIN_LIFECYCLE_BLOCKED;
    EXPECT_RESULT(wt_lifecycle_select_next(slots, 4U, 0U, &selected),
                  WT_LIFECYCLE_ERROR_NO_READY);
}

static void wt_test_arguments(void)
{
    wt_lifecycle_runtime_t runtime = {
        WT_DOMAIN_LIFECYCLE_STOPPED, 0U, 0U
    };
    wt_lifecycle_policy_t policy = wt_domain_policy();
    wt_lifecycle_slot_t slot = {
        WT_DOMAIN_LIFECYCLE_STOPPED, 0U
    };
    size_t selected = 0U;

    EXPECT_RESULT(wt_lifecycle_transition(NULL, &policy,
                                          WT_LIFECYCLE_EVENT_START, 0U),
                  WT_LIFECYCLE_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_lifecycle_transition(&runtime, NULL,
                                          WT_LIFECYCLE_EVENT_START, 0U),
                  WT_LIFECYCLE_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_lifecycle_select_next(NULL, 1U, 0U, &selected),
                  WT_LIFECYCLE_ERROR_ARGUMENT);
    EXPECT_RESULT(wt_lifecycle_select_next(&slot, 1U, 0U, NULL),
                  WT_LIFECYCLE_ERROR_ARGUMENT);
}

int main(void)
{
    wt_test_lifecycle_flow();
    wt_test_restart_containment();
    wt_test_terminal_policy();
    wt_test_selector();
    wt_test_arguments();
    if (g_failures != 0U) {
        (void)fprintf(stderr, "lifecycle checks failed: %u/%u\n",
                      g_failures, g_checks);
        return 1;
    }
    (void)printf("lifecycle checks passed: %u\n", g_checks);
    return 0;
}
