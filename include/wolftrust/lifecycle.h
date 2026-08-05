/* lifecycle.h
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

#ifndef WOLFTRUST_LIFECYCLE_H
#define WOLFTRUST_LIFECYCLE_H

#include <stddef.h>
#include <stdint.h>

#include "wolftrust/domain.h"

typedef enum wt_lifecycle_event {
    WT_LIFECYCLE_EVENT_INITIALIZE = 0,
    WT_LIFECYCLE_EVENT_START,
    WT_LIFECYCLE_EVENT_WAIT,
    WT_LIFECYCLE_EVENT_SIGNAL,
    WT_LIFECYCLE_EVENT_PREEMPT,
    WT_LIFECYCLE_EVENT_PANIC,
    WT_LIFECYCLE_EVENT_RESTART,
    WT_LIFECYCLE_EVENT_STOP
} wt_lifecycle_event_t;

typedef struct wt_lifecycle_runtime {
    wt_domain_lifecycle_t state;
    uint32_t restart_count;
    uint32_t first_restart_tick;
} wt_lifecycle_runtime_t;

typedef struct wt_lifecycle_policy {
    wt_restart_action_t action;
    uint32_t restart_limit;
    uint32_t restart_window_ticks;
} wt_lifecycle_policy_t;

typedef struct wt_lifecycle_slot {
    wt_domain_lifecycle_t state;
    uint32_t priority;
} wt_lifecycle_slot_t;

typedef enum wt_lifecycle_result {
    WT_LIFECYCLE_VALID = 0,
    WT_LIFECYCLE_ERROR_ARGUMENT = -200,
    WT_LIFECYCLE_ERROR_POLICY = -201,
    WT_LIFECYCLE_ERROR_TRANSITION = -202,
    WT_LIFECYCLE_ERROR_TERMINAL = -203,
    WT_LIFECYCLE_ERROR_NO_READY = -204
} wt_lifecycle_result_t;

/* Apply one SPM-managed lifecycle event. Invalid transitions leave runtime
 * unchanged. A panic that exhausts the declared domain restart budget enters
 * FAULTED and returns WT_LIFECYCLE_ERROR_TERMINAL. */
int wt_lifecycle_transition(wt_lifecycle_runtime_t* runtime,
                            const wt_lifecycle_policy_t* policy,
                            wt_lifecycle_event_t event,
                            uint32_t tick);

/* Select the highest-priority READY slot, using start_index for round-robin
 * tie breaking. The output is an array index, not a domain identifier. */
int wt_lifecycle_select_next(const wt_lifecycle_slot_t* slots,
                             size_t slot_count,
                             size_t start_index,
                             size_t* selected_index);

#endif
