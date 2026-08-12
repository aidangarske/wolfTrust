/* spm_svc.c
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

/* P1t (WT-FFM-0014/0011): run a Secure Partition as an unprivileged scheduled
 * coroutine. The partition's service loop is the same architecture-neutral
 * code the host tests prove (src/services/crypto_service.c); only the
 * transport differs — every wt_spm_call_t traps here via SVC so the gate,
 * pointer validation, and any blocking run privileged on the SPM side while
 * the partition thread stays unprivileged inside its manifest MPU domain. */

#include "wolftrust/arch/armv8m/spm_svc.h"

#include "wolftrust/ffm_domain.h"
#include "wolftrust/platform.h"
#include "wolftrust/sched/coroutine.h"
#include "wolftrust/services/crypto_service.h"
#include "wolftrust/spm_gate.h"

#include "memory_map.h"

/* Single scheduled SP today (crypto). P3 generalizes this to a table when
 * the Arm conformance partitions arrive. */
static wt_ffm_runtime_t* g_spm_svc_runtime;
static wt_co_t* g_spm_sp_co;
static wt_secure_domain_t g_spm_sp_domain;
static wt_secure_domain_t g_spm_sp_table;

/* Privileged SVC #1 dispatcher, tail-called from SVC_Handler with r0 = the
 * exception frame. Validates that the request comes from the scheduled SP
 * coroutine and that the call struct lies inside that partition's writable
 * domain, then runs the gate. A psa_wait with nothing asserted suspends the
 * coroutine; the SP-side transport re-issues the SVC on wake. The gate-level
 * status is written back through the stacked r0. */
__attribute__((used))
void wt_spm_svc_entry(uint32_t* frame)
{
    wt_spm_call_t* call;
    int status;

    call = (wt_spm_call_t*)(uintptr_t)frame[0];
    if (g_spm_svc_runtime == NULL || g_spm_sp_co == NULL ||
            wt_co_current() != g_spm_sp_co ||
            wt_secure_domain_contains(&g_spm_sp_table, (uintptr_t)call,
                                      sizeof(*call), 1) == 0) {
        frame[0] = (uint32_t)WT_FFM_ERROR_ARGUMENT;
        return;
    }

    status = wt_spm_gate(g_spm_svc_runtime, &g_spm_sp_table, call);
    frame[0] = (uint32_t)status;
    if (status == WT_FFM_SUCCESS && wt_spm_call_would_block(call)) {
        /* Suspends after the SVC returns (PendSV tail-chains); execution
         * resumes at the instruction after `svc` when a signal wakes us. */
        wt_co_block();
    }
}

/* r0 = call in, r0 = gate-level status out (written into the stacked frame
 * by the privileged dispatcher). */
__attribute__((naked))
static int wt_spm_svc_raw(wt_spm_call_t* call __attribute__((unused)))
{
    __asm__ volatile (
        "svc  #1   \n"
        "bx   lr   \n"
    );
}

/* SP-side transport: trap each request to the privileged gate. A blocking
 * psa_wait comes back with the stale NOT_READY result after the coroutine
 * is rewoken, so re-issue until the wait really completes. */
static int wt_spm_svc_transport(wt_ffm_runtime_t* runtime, wt_spm_call_t* call)
{
    int status;

    (void)runtime;
    for (;;) {
        status = wt_spm_svc_raw(call);
        if (status != WT_FFM_SUCCESS || call->op != WT_SPM_OP_WAIT ||
                call->ret_int != WT_FFM_ERROR_NOT_READY) {
            break;
        }
    }
    return status;
}

/* The scheduled Secure Partition thread: the production service loop,
 * unprivileged on its own stack, one message per iteration. The runtime
 * pointer is never dereferenced on this side — every request crosses the
 * SVC transport. */
static void wt_spm_sp_entry(void* arg)
{
    int32_t partition_id = (int32_t)(intptr_t)arg;
    /* Transport + compute live on the SP's own stack: the service loop must
     * not read the file-scope globals, which sit in SPM RAM the unprivileged
     * partition cannot map. Both are flash code addresses, so building the
     * struct touches only the partition's mapped stack and code. */
    wt_crypto_service_ctx_t ctx;

    ctx.transport = wt_spm_svc_transport;
    ctx.compute = wt_crypto_sp_hash;

    for (;;) {
#if defined(WT_FFM_NEGATIVE_PROBE) && (WT_FFM_NEGATIVE_PROBE == 1)
        /* Negative isolation proof (WT-FFM-0011): an unprivileged read of
         * SPM-private RAM from inside the SP domain must MemManage-fault.
         * Never built into production images. */
        volatile uint32_t probe;
        probe = *(const volatile uint32_t*)(uintptr_t)WT_RAM_S_BASE;
        (void)probe;
#endif
        (void)wt_crypto_service_dispatch(&ctx, NULL, partition_id);
    }
}

/* Manifest-bound dispatch for the scheduled SP: assertively wake the
 * coroutine and drive it until it blocks on its next psa_wait, at which
 * point the enqueued message has been replied to (the FF-M core verifies
 * message completion after this returns). Runs on the bootstrap context. */
static int wt_spm_sched_dispatch(void* context, wt_ffm_runtime_t* runtime,
                                 int32_t partition_id)
{
    wt_co_t* co = (wt_co_t*)context;

    (void)runtime;
    (void)partition_id;
    if (co == NULL || wt_co_state(co) == WT_CO_FAULTED) {
        return WT_FFM_ERROR_STATE;
    }
    wt_co_wake(co);
    while (wt_co_state(co) == WT_CO_RUNNABLE) {
        if (wt_co_run(co) == 0u) {
            return WT_FFM_ERROR_STATE;
        }
    }
    return wt_co_state(co) == WT_CO_BLOCKED ? WT_FFM_SUCCESS :
                                              WT_FFM_ERROR_STATE;
}

int wt_spm_sched_start(wt_ffm_runtime_t* runtime, int32_t partition_id)
{
    const wt_mpu_region_t* stack_region;
    size_t region_count;
    size_t i;

    if (runtime == NULL || runtime->manifest == NULL) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    if (g_spm_sp_co != NULL) {
        return WT_FFM_ERROR_STATE;
    }
    if (wt_ffm_resolve_secure_domain(runtime->manifest,
                                     (wt_domain_id_t)partition_id,
                                     &g_spm_sp_domain) !=
            WT_SECURE_DOMAIN_OK) {
        return WT_FFM_ERROR_STATE;
    }

    /* The SP's execution stack is the manifest domain's writable resource;
     * fail closed if the manifest stops declaring one. */
    stack_region = NULL;
    for (i = 0u; i < g_spm_sp_domain.region_count; i++) {
        if ((g_spm_sp_domain.regions[i].attributes & WT_MEM_ATTR_WRITE) !=
                0u &&
                (g_spm_sp_domain.regions[i].attributes &
                 WT_MEM_ATTR_DEVICE) == 0u) {
            stack_region = &g_spm_sp_domain.regions[i];
        }
    }
    if (stack_region == NULL) {
        return WT_FFM_ERROR_STATE;
    }

    /* Thread-domain MPU table: shared whole-image RX (the manifest's 4K code
     * window lies inside it and Armv8-M regions must not overlap — task #26
     * tracks narrowing) plus the domain's non-EXEC resources. */
    g_spm_sp_table.domain_id = g_spm_sp_domain.domain_id;
    g_spm_sp_table.regions[0].base = WT_FLASH_S_BASE;
    g_spm_sp_table.regions[0].size = WT_FLASH_S_SIZE;
    g_spm_sp_table.regions[0].attributes = WT_MEM_ATTR_READ |
                                           WT_MEM_ATTR_EXEC;
    region_count = 1u;
    for (i = 0u; i < g_spm_sp_domain.region_count &&
            region_count < WT_MAX_MPU_REGIONS; i++) {
        if ((g_spm_sp_domain.regions[i].attributes & WT_MEM_ATTR_EXEC) ==
                0u) {
            g_spm_sp_table.regions[region_count] = g_spm_sp_domain.regions[i];
            region_count++;
        }
    }
    g_spm_sp_table.region_count = region_count;

    g_spm_sp_co = wt_co_create_blocked_ex(
        (uint8_t*)(uintptr_t)stack_region->base, stack_region->size,
        wt_spm_sp_entry, (void*)(intptr_t)partition_id);
    if (g_spm_sp_co == NULL) {
        return WT_FFM_ERROR_RESOURCE;
    }
    wt_co_set_domain(g_spm_sp_co, &g_spm_sp_table, 1u);

    /* Transport and compute reach the SP through the dispatch context it
     * builds on its own stack (wt_spm_sp_entry), so no global-pointer read
     * crosses the partition's MPU domain. The one-shot MSP trampoline the
     * privileged path installed would panic on a PSP thread, so it is not
     * used here. */
    g_spm_svc_runtime = runtime;
    return wt_ffm_register_partition(runtime, partition_id,
                                     wt_spm_sched_dispatch, g_spm_sp_co);
}
