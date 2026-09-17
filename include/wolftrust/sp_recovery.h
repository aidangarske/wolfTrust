/* sp_recovery.h
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

#ifndef WOLFTRUST_SP_RECOVERY_H
#define WOLFTRUST_SP_RECOVERY_H

#include <stdint.h>

#include "wolftrust/domain.h"

/* Graceful Secure-Partition fault recovery (WT-SYS-0008 / WT-FFM-0017).
 *
 * When a Secure Partition faults, wolfTrust does not reset the world. It
 * releases the partition's held locks, unblocks any pinned client with a
 * defined error, scrubs the partition's private memory, and — if the manifest
 * restart policy allows and the restart budget is not spent — restarts the
 * partition in place. Only a non-restartable (NEVER), platform-fatal
 * (PLATFORM), or budget-exhausted fault escalates to fail-closed.
 *
 * The decision and sequencing are architecture-neutral and host-tested; the
 * arch/port injects the concrete cleanup steps through wt_sp_recovery_ops. */

typedef enum wt_sp_recovery_decision {
    WT_SP_RECOVERY_RESTART = 0,
    WT_SP_RECOVERY_ESCALATE
} wt_sp_recovery_decision_t;

/* Concrete, arch-specific cleanup steps. Every hook is optional (NULL is a
 * no-op) so a host test can supply only the ones it observes. `restart`
 * returns 0 on success; a non-zero return turns a RESTART decision into an
 * ESCALATE so a partition that cannot be re-armed is never left half-dead. */
typedef struct wt_sp_recovery_ops {
    void (*release_locks)(void* ctx);
    void (*fail_messages)(void* ctx);
    void (*scrub_domain)(void* ctx);
    int  (*restart)(void* ctx);
    void (*escalate)(void* ctx, wt_restart_action_t action);
} wt_sp_recovery_ops_t;

/* Decide RESTART vs ESCALATE for one fault. NEVER and PLATFORM escalate
 * without touching the budget counters; DOMAIN defers to the shared restart
 * budget engine (wt_restart_policy_evaluate), which advances the counters on a
 * RESTART and leaves them unchanged on exhaustion. */
wt_sp_recovery_decision_t wt_sp_recovery_decide(
    wt_restart_action_t action,
    uint32_t restart_limit, uint32_t restart_window_ticks,
    uint32_t monotonic_ticks,
    uint32_t* restart_count, uint32_t* first_restart_tick);

/* Run the full recovery sequence for one fault and return the decision.
 * Ordering is security-critical: locks are dropped and pinned clients are
 * failed BEFORE the restart decision so no client stays blocked and no woken
 * waiter contends a lock still held by the dead partition — this holds on both
 * the RESTART and the ESCALATE path. On RESTART the domain is scrubbed then the
 * partition is re-armed; on ESCALATE (including a failed restart) the escalate
 * hook fires with the manifest action so the port can quarantine the one
 * partition or, for a platform-fatal service, fail the platform closed. */
wt_sp_recovery_decision_t wt_sp_recovery_run(
    const wt_sp_recovery_ops_t* ops, void* ctx,
    wt_restart_action_t action,
    uint32_t restart_limit, uint32_t restart_window_ticks,
    uint32_t monotonic_ticks,
    uint32_t* restart_count, uint32_t* first_restart_tick);

#endif
