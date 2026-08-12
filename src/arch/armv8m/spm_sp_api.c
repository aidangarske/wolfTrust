/* spm_sp_api.c
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

/* Secure-Partition-side psa_* API (WT-FFM-0014): every call marshals a
 * wt_spm_call_t and traps to the privileged gate via SVC, so unmodified Arm
 * partition code links these symbols and runs unprivileged inside its manifest
 * MPU domain. Replaces the direct src/ffm_api.c implementations in the target
 * image — those touch SPM state an unprivileged thread cannot reach. */

#include "psa/client.h"
#include "psa/lifecycle.h"
#include "psa/service.h"

#include "wolftrust/arch/armv8m/spm_svc.h"
#include "wolftrust/spm_gate.h"

/* A failed service-side call is a programmer error: hang the partition so the
 * SPM's fault/restart policy deals with it instead of running on bad state. */
__attribute__((noreturn))
static void wt_sp_api_panic(void)
{
    for (;;) {
    }
}

psa_signal_t psa_wait(psa_signal_t signal_mask, uint32_t timeout)
{
    wt_spm_call_t call;
    psa_signal_t asserted = 0U;

    /* PSA_POLL timing is tracked with the psa_wait timeout task; every
     * partition loop in the suite waits with PSA_BLOCK. */
    (void)timeout;
    call.op = WT_SPM_OP_WAIT;
    call.signal_mask = signal_mask;
    call.asserted = &asserted;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return 0U;
    }
    return asserted;
}

psa_status_t psa_get(psa_signal_t signal, psa_msg_t* msg)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_GET;
    call.signal = signal;
    call.msg = msg;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS) {
        return PSA_ERROR_PROGRAMMER_ERROR;
    }
    return call.ret_status;
}

void psa_set_rhandle(psa_handle_t msg_handle, void* rhandle)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_SET_RHANDLE;
    call.msg_handle = msg_handle;
    call.rhandle = rhandle;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        wt_sp_api_panic();
    }
}

size_t psa_read(psa_handle_t msg_handle, uint32_t invec_idx,
                void* buffer, size_t num_bytes)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_READ;
    call.msg_handle = msg_handle;
    call.vec_idx = invec_idx;
    call.buffer = buffer;
    call.num_bytes = num_bytes;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS) {
        return 0U;
    }
    return call.ret_size;
}

size_t psa_skip(psa_handle_t msg_handle, uint32_t invec_idx, size_t num_bytes)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_SKIP;
    call.msg_handle = msg_handle;
    call.vec_idx = invec_idx;
    call.num_bytes = num_bytes;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS) {
        return 0U;
    }
    return call.ret_size;
}

void psa_write(psa_handle_t msg_handle, uint32_t outvec_idx,
               const void* buffer, size_t num_bytes)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_WRITE;
    call.msg_handle = msg_handle;
    call.vec_idx = outvec_idx;
    call.buffer = (void*)(uintptr_t)buffer;
    call.num_bytes = num_bytes;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        wt_sp_api_panic();
    }
}

void psa_reply(psa_handle_t msg_handle, psa_status_t status)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_REPLY;
    call.msg_handle = msg_handle;
    call.status = status;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS ||
            call.ret_status != PSA_SUCCESS) {
        wt_sp_api_panic();
    }
}

void psa_notify(int32_t partition_id)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_NOTIFY;
    call.notify_partition = partition_id;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        wt_sp_api_panic();
    }
}

void psa_clear(void)
{
    wt_spm_call_t call;

    call.op = WT_SPM_OP_CLEAR;
    if (wt_spm_sp_call(&call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        wt_sp_api_panic();
    }
}

void psa_eoi(psa_signal_t irq_signal)
{
    (void)irq_signal;
    wt_sp_api_panic();
}

void psa_panic(void)
{
    wt_sp_api_panic();
}

uint32_t psa_rot_lifecycle_state(void)
{
    return PSA_LIFECYCLE_UNKNOWN;
}

uint32_t psa_framework_version(void)
{
    return PSA_FRAMEWORK_VERSION;
}

/* SP-as-client IPC (a partition connecting to another partition's service)
 * has no gate ops yet; refuse instead of faking success. Needed by the PAL
 * print/NVM plane, tracked with the driver-partition task. */
uint32_t psa_version(uint32_t sid)
{
    (void)sid;
    return PSA_VERSION_NONE;
}

psa_handle_t psa_connect(uint32_t sid, uint32_t version)
{
    (void)sid;
    (void)version;
    return (psa_handle_t)PSA_ERROR_CONNECTION_REFUSED;
}

psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec* in_vec, size_t in_len,
                      psa_outvec* out_vec, size_t out_len)
{
    (void)handle;
    (void)type;
    (void)in_vec;
    (void)in_len;
    (void)out_vec;
    (void)out_len;
    return PSA_ERROR_PROGRAMMER_ERROR;
}

void psa_close(psa_handle_t handle)
{
    (void)handle;
}
