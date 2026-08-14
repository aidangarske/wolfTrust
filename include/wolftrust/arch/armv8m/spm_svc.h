/* spm_svc.h
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

#ifndef WOLFTRUST_ARCH_ARMV8M_SPM_SVC_H
#define WOLFTRUST_ARCH_ARMV8M_SPM_SVC_H

#include "wolftrust/ffm.h"

/* SVC immediate for a Secure Partition psa_* request. 0x7F is the NS-guest
 * return path; anything else falls through to the PendSV scheduler pend. */
#define WT_SVC_SPM_CALL 0x01

/* A scheduled Secure Partition's thread entry: the partition's service loop,
 * unprivileged on its own stack, reaching the SPM only through the SVC
 * transport. arg is the partition id passed to wt_spm_sched_add. */
typedef void (*wt_spm_sp_entry_fn)(void* arg);

/* Schedule one Secure Partition (P3a): resolve its manifest protection domain,
 * build the unprivileged MPU thread table, create the coroutine on the manifest
 * stack running `entry`, and register the partition's dispatch as wake-and-run.
 * The scheduler holds a table of up to WT_FFM_MAX_PARTITIONS SPs, each keyed by
 * its coroutine so the SVC dispatcher resolves the caller from wt_co_current().
 * Call after wt_tasklet_init and wt_ffm_boot_init. Fails closed. */
int wt_spm_sched_add(wt_ffm_runtime_t* runtime, int32_t partition_id,
                     wt_spm_sp_entry_fn entry, void* arg);

/* Start the crypto Secure Partition as a scheduled coroutine (P1t): the single
 * built-in SP, scheduled via wt_spm_sched_add with the crypto service loop. */
int wt_spm_sched_start(wt_ffm_runtime_t* runtime, int32_t partition_id);

/* SP-side transport: trap one wt_spm_call_t to the privileged gate via SVC,
 * re-issuing a blocking psa_wait after each wake until it completes. The SVC
 * dispatcher stamps the caller's own partition id into the call, so callers
 * need not (and cannot usefully) set it. Only valid on a scheduled SP thread. */
struct wt_spm_call;
int wt_spm_sp_call(struct wt_spm_call* call);

/* Privileged SVC #1 dispatcher. Tail-called from SVC_Handler asm with
 * r0 = the exception frame; not for direct C callers. */
void wt_spm_svc_entry(uint32_t* frame);

#if defined(WT_CONFORMANCE) && (WT_CONFORMANCE == 1)
/* Conformance-only hang tripwire: called from the secure SysTick; traps with
 * a scheduler-state register dump when IPC activity stalls (lost wake). */
void wt_spm_sched_hang_probe(void);
#endif

#endif /* WOLFTRUST_ARCH_ARMV8M_SPM_SVC_H */
