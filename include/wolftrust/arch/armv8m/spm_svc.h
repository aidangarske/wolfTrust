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
#include "wolftrust/spm_sched.h"

/* SVC immediate for a Secure Partition psa_* request. 0x7F is the NS-guest
 * return path; anything else falls through to the PendSV scheduler pend. */
#define WT_SVC_SPM_CALL 0x01

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

/* FLIH for a Secure Partition interrupt (P4.2c): masks the line, resolves the
 * manifest-bound partition/signal for `irq`, and asserts the signal so the
 * partition's psa_wait observes it. Called from the port's IRQ vector. */
void wt_spm_conf_irq(uint32_t irq);
#endif

#endif /* WOLFTRUST_ARCH_ARMV8M_SPM_SVC_H */
