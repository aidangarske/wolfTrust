/* restart_policy.h
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

#ifndef WOLFTRUST_RESTART_POLICY_H
#define WOLFTRUST_RESTART_POLICY_H

#include <stdint.h>

typedef enum wt_restart_decision {
    WT_RESTART_DECISION_RESTART = 0,
    WT_RESTART_DECISION_FAULT
} wt_restart_decision_t;

/* Evaluate the per-domain restart budget for one fault. On entry *restart_count
 * and *first_restart_tick hold the domain's running budget state; on a RESTART
 * decision they are advanced (count incremented, first_restart_tick set to the
 * current tick, which the window check reads as the last-restart tick). A zero
 * restart_limit means unlimited restarts. A budget exhausted within the window
 * returns WT_RESTART_DECISION_FAULT and leaves the counters unchanged. */
wt_restart_decision_t wt_restart_policy_evaluate(uint32_t restart_limit,
                                                 uint32_t restart_window_ticks,
                                                 uint32_t monotonic_ticks,
                                                 uint32_t* restart_count,
                                                 uint32_t* first_restart_tick);

#endif
