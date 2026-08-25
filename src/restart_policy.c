/* restart_policy.c
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

#include "wolftrust/restart_policy.h"

#include <stddef.h>

wt_restart_decision_t wt_restart_policy_evaluate(uint32_t restart_limit,
                                                 uint32_t restart_window_ticks,
                                                 uint32_t monotonic_ticks,
                                                 uint32_t* restart_count,
                                                 uint32_t* first_restart_tick)
{
    if (restart_count == NULL || first_restart_tick == NULL) {
        return WT_RESTART_DECISION_FAULT;
    }

    if (restart_limit > 0U) {
        /* Reset the restart budget only after a full crash-free window since
         * the LAST restart (field holds the last-restart tick): wall-time
         * alone must not clear a crash-looping guest, or a reboot cycle
         * slower than the window would evade the limit forever on silicon. */
        if (*restart_count > 0U && restart_window_ticks > 0U &&
                (monotonic_ticks - *first_restart_tick) >= restart_window_ticks) {
            *restart_count = 0U;
        }
        if (*restart_count >= restart_limit) {
            return WT_RESTART_DECISION_FAULT;
        }
    }

    (*restart_count)++;
    *first_restart_tick = monotonic_ticks;
    return WT_RESTART_DECISION_RESTART;
}
