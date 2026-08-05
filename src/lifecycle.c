/* lifecycle.c
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

static int wt_lifecycle_policy_valid(const wt_lifecycle_policy_t* policy)
{
    if (policy == NULL || (unsigned int)policy->action >
            (unsigned int)WT_RESTART_ACTION_PLATFORM) {
        return WT_LIFECYCLE_ERROR_POLICY;
    }

    if (policy->action == WT_RESTART_ACTION_DOMAIN &&
            (policy->restart_limit == 0U ||
             policy->restart_window_ticks == 0U)) {
        return WT_LIFECYCLE_ERROR_POLICY;
    }

    if (policy->action != WT_RESTART_ACTION_DOMAIN &&
            (policy->restart_limit != 0U ||
             policy->restart_window_ticks != 0U)) {
        return WT_LIFECYCLE_ERROR_POLICY;
    }

    return WT_LIFECYCLE_VALID;
}

static int wt_lifecycle_state_valid(wt_domain_lifecycle_t state)
{
    return (unsigned int)state <=
           (unsigned int)WT_DOMAIN_LIFECYCLE_RESTARTING;
}

int wt_lifecycle_transition(wt_lifecycle_runtime_t* runtime,
                            const wt_lifecycle_policy_t* policy,
                            wt_lifecycle_event_t event,
                            uint32_t tick)
{
    wt_domain_lifecycle_t next_state;
    int policy_result;

    if (runtime == NULL || policy == NULL ||
            (unsigned int)event > (unsigned int)WT_LIFECYCLE_EVENT_STOP ||
            !wt_lifecycle_state_valid(runtime->state)) {
        return WT_LIFECYCLE_ERROR_ARGUMENT;
    }

    policy_result = wt_lifecycle_policy_valid(policy);
    if (policy_result != WT_LIFECYCLE_VALID)
        return policy_result;

    next_state = runtime->state;
    switch (event) {
        case WT_LIFECYCLE_EVENT_INITIALIZE:
            if (runtime->state != WT_DOMAIN_LIFECYCLE_STOPPED)
                return WT_LIFECYCLE_ERROR_TRANSITION;
            next_state = WT_DOMAIN_LIFECYCLE_READY;
            break;

        case WT_LIFECYCLE_EVENT_START:
            if (runtime->state != WT_DOMAIN_LIFECYCLE_READY)
                return WT_LIFECYCLE_ERROR_TRANSITION;
            next_state = WT_DOMAIN_LIFECYCLE_RUNNING;
            break;

        case WT_LIFECYCLE_EVENT_WAIT:
            if (runtime->state != WT_DOMAIN_LIFECYCLE_RUNNING)
                return WT_LIFECYCLE_ERROR_TRANSITION;
            next_state = WT_DOMAIN_LIFECYCLE_BLOCKED;
            break;

        case WT_LIFECYCLE_EVENT_SIGNAL:
            if (runtime->state != WT_DOMAIN_LIFECYCLE_BLOCKED)
                return WT_LIFECYCLE_ERROR_TRANSITION;
            next_state = WT_DOMAIN_LIFECYCLE_READY;
            break;

        case WT_LIFECYCLE_EVENT_PREEMPT:
            if (runtime->state != WT_DOMAIN_LIFECYCLE_RUNNING)
                return WT_LIFECYCLE_ERROR_TRANSITION;
            next_state = WT_DOMAIN_LIFECYCLE_READY;
            break;

        case WT_LIFECYCLE_EVENT_PANIC:
            if (runtime->state == WT_DOMAIN_LIFECYCLE_STOPPED ||
                    runtime->state == WT_DOMAIN_LIFECYCLE_FAULTED) {
                return WT_LIFECYCLE_ERROR_TRANSITION;
            }
            if (policy->action != WT_RESTART_ACTION_DOMAIN) {
                runtime->state = WT_DOMAIN_LIFECYCLE_FAULTED;
                return WT_LIFECYCLE_ERROR_TERMINAL;
            }
            if (runtime->restart_count == 0U ||
                    tick - runtime->first_restart_tick >=
                        policy->restart_window_ticks) {
                runtime->restart_count = 0U;
                runtime->first_restart_tick = tick;
            }
            if (runtime->restart_count >= policy->restart_limit ||
                    runtime->restart_count == UINT32_MAX) {
                runtime->state = WT_DOMAIN_LIFECYCLE_FAULTED;
                return WT_LIFECYCLE_ERROR_TERMINAL;
            }
            runtime->restart_count++;
            runtime->first_restart_tick =
                runtime->restart_count == 1U ? tick : runtime->first_restart_tick;
            next_state = WT_DOMAIN_LIFECYCLE_RESTARTING;
            break;

        case WT_LIFECYCLE_EVENT_RESTART:
            if (runtime->state != WT_DOMAIN_LIFECYCLE_RESTARTING)
                return WT_LIFECYCLE_ERROR_TRANSITION;
            next_state = WT_DOMAIN_LIFECYCLE_READY;
            break;

        case WT_LIFECYCLE_EVENT_STOP:
            if (runtime->state == WT_DOMAIN_LIFECYCLE_STOPPED)
                return WT_LIFECYCLE_ERROR_TRANSITION;
            next_state = WT_DOMAIN_LIFECYCLE_STOPPED;
            break;
    }

    runtime->state = next_state;
    return WT_LIFECYCLE_VALID;
}

int wt_lifecycle_select_next(const wt_lifecycle_slot_t* slots,
                             size_t slot_count,
                             size_t start_index,
                             size_t* selected_index)
{
    uint32_t highest_priority = 0U;
    int found = 0;
    size_t offset;

    if (slots == NULL || selected_index == NULL || slot_count == 0U)
        return WT_LIFECYCLE_ERROR_ARGUMENT;

    start_index %= slot_count;
    for (offset = 0U; offset < slot_count; offset++) {
        size_t index = (start_index + offset) % slot_count;

        if (slots[index].state == WT_DOMAIN_LIFECYCLE_READY &&
                (!found || slots[index].priority > highest_priority)) {
            highest_priority = slots[index].priority;
            found = 1;
        }
    }

    if (!found)
        return WT_LIFECYCLE_ERROR_NO_READY;

    for (offset = 0U; offset < slot_count; offset++) {
        size_t index = (start_index + offset) % slot_count;

        if (slots[index].state == WT_DOMAIN_LIFECYCLE_READY &&
                slots[index].priority == highest_priority) {
            *selected_index = index;
            return WT_LIFECYCLE_VALID;
        }
    }

    return WT_LIFECYCLE_ERROR_NO_READY;
}
