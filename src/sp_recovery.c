/* sp_recovery.c
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

#include "wolftrust/sp_recovery.h"

#include "wolftrust/restart_policy.h"

wt_sp_recovery_decision_t wt_sp_recovery_decide(
    wt_restart_action_t action,
    uint32_t restart_limit, uint32_t restart_window_ticks,
    uint32_t monotonic_ticks,
    uint32_t* restart_count, uint32_t* first_restart_tick)
{
    if (restart_count == NULL || first_restart_tick == NULL) {
        return WT_SP_RECOVERY_ESCALATE;
    }
    /* A partition that must never restart, or whose fault the manifest declares
     * platform-fatal, escalates immediately without spending budget. */
    if (action != WT_RESTART_ACTION_DOMAIN) {
        return WT_SP_RECOVERY_ESCALATE;
    }
    if (wt_restart_policy_evaluate(restart_limit, restart_window_ticks,
                                   monotonic_ticks, restart_count,
                                   first_restart_tick) ==
            WT_RESTART_DECISION_RESTART) {
        return WT_SP_RECOVERY_RESTART;
    }
    return WT_SP_RECOVERY_ESCALATE;
}

wt_sp_recovery_decision_t wt_sp_recovery_run(
    const wt_sp_recovery_ops_t* ops, void* ctx,
    wt_restart_action_t action,
    uint32_t restart_limit, uint32_t restart_window_ticks,
    uint32_t monotonic_ticks,
    uint32_t* restart_count, uint32_t* first_restart_tick)
{
    wt_sp_recovery_decision_t decision;

    if (ops == NULL) {
        return WT_SP_RECOVERY_ESCALATE;
    }

    /* Release locks and unblock pinned clients first, before the restart
     * decision, so both the RESTART and ESCALATE paths leave no held lock and
     * no client hung on a reply that will never come. */
    if (ops->release_locks != NULL) {
        ops->release_locks(ctx);
    }
    if (ops->fail_messages != NULL) {
        ops->fail_messages(ctx);
    }

    decision = wt_sp_recovery_decide(action, restart_limit,
                                     restart_window_ticks, monotonic_ticks,
                                     restart_count, first_restart_tick);

    if (decision == WT_SP_RECOVERY_RESTART) {
        if (ops->scrub_domain != NULL) {
            ops->scrub_domain(ctx);
        }
        if (ops->restart != NULL && ops->restart(ctx) != 0) {
            /* A partition that cannot be re-armed must not be left half-dead. */
            decision = WT_SP_RECOVERY_ESCALATE;
        }
    }

    if (decision == WT_SP_RECOVERY_ESCALATE && ops->escalate != NULL) {
        ops->escalate(ctx, action);
    }
    return decision;
}
