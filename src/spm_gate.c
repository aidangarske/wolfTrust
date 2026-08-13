/* spm_gate.c
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

#include "wolftrust/spm_gate.h"

static int wt_spm_check_buffer(const wt_secure_domain_t* domain,
                               const void* buffer, size_t len, int need_write)
{
    if (domain == NULL)
        return WT_FFM_SUCCESS; /* validation disabled: caller owns the range */
    if (buffer == NULL)
        return WT_FFM_ERROR_BUFFER;
    if (wt_secure_domain_contains(domain, (uintptr_t)buffer, len, need_write)
            == 0)
        return WT_FFM_ERROR_BUFFER;
    return WT_FFM_SUCCESS;
}

/* A resumed CONNECT/CALL/CLOSE may only harvest a message the acting
 * partition itself created; a forged pending_msg cannot reach another
 * caller's message. */
static int wt_spm_pending_owned(const wt_ffm_runtime_t* runtime,
                                const wt_spm_call_t* call)
{
    if (call->pending_msg >= WT_FFM_MAX_MESSAGES)
        return 0;
    if (runtime->messages[call->pending_msg].allocated == 0U)
        return 0;
    return runtime->messages[call->pending_msg].caller ==
           (psa_client_id_t)call->partition_id;
}

/* Every SP-supplied vector base must lie inside the acting partition's own
 * domain before the SPM captures or writes it. */
static int wt_spm_check_sp_vectors(const wt_secure_domain_t* domain,
                                   const wt_spm_call_t* call)
{
    size_t i;
    int ret;

    if (call->sp_in_len > WT_SPM_SP_IOVEC ||
            call->sp_out_len > WT_SPM_SP_IOVEC)
        return WT_FFM_ERROR_ARGUMENT;
    for (i = 0U; i < call->sp_in_len; i++) {
        if (call->sp_in[i].len == 0U)
            continue;
        ret = wt_spm_check_buffer(domain, call->sp_in[i].base,
                                  call->sp_in[i].len, 0);
        if (ret != WT_FFM_SUCCESS)
            return ret;
    }
    for (i = 0U; i < call->sp_out_len; i++) {
        if (call->sp_out[i].len == 0U)
            continue;
        ret = wt_spm_check_buffer(domain, call->sp_out[i].base,
                                  call->sp_out[i].len, 1);
        if (ret != WT_FFM_SUCCESS)
            return ret;
    }
    return WT_FFM_SUCCESS;
}

int wt_spm_gate(wt_ffm_runtime_t* runtime,
                const wt_secure_domain_t* caller_domain,
                wt_spm_call_t* call)
{
    int ret;

    if (runtime == NULL || call == NULL)
        return WT_FFM_ERROR_ARGUMENT;

    call->ret_status = PSA_ERROR_PROGRAMMER_ERROR;
    call->ret_size = 0U;
    call->ret_int = WT_FFM_ERROR_ARGUMENT;

    switch (call->op) {
    case WT_SPM_OP_WAIT:
        ret = wt_spm_check_buffer(caller_domain, call->asserted,
                                  sizeof(*call->asserted), 1);
        if (ret == WT_FFM_SUCCESS)
            ret = wt_ffm_wait(runtime, call->partition_id, call->signal_mask,
                              call->asserted);
        call->ret_int = ret;
        break;
    case WT_SPM_OP_GET:
        ret = wt_spm_check_buffer(caller_domain, call->msg, sizeof(*call->msg),
                                  1);
        if (ret == WT_FFM_SUCCESS)
            call->ret_status = wt_ffm_get(runtime, call->partition_id,
                                          call->signal, call->msg);
        else
            call->ret_status = (psa_status_t)ret;
        call->ret_int = ret;
        break;
    case WT_SPM_OP_SET_RHANDLE:
        call->ret_int = wt_ffm_set_rhandle(runtime, call->partition_id,
                                           call->msg_handle, call->rhandle);
        break;
    case WT_SPM_OP_READ:
        ret = wt_spm_check_buffer(caller_domain, call->buffer, call->num_bytes,
                                  1);
        if (ret == WT_FFM_SUCCESS) {
            call->ret_size = wt_ffm_read(runtime, call->partition_id,
                                         call->msg_handle, call->vec_idx,
                                         call->buffer, call->num_bytes);
            call->ret_int = WT_FFM_SUCCESS;
        } else {
            call->ret_int = ret;
        }
        break;
    case WT_SPM_OP_SKIP:
        call->ret_size = wt_ffm_skip(runtime, call->partition_id,
                                     call->msg_handle, call->vec_idx,
                                     call->num_bytes);
        call->ret_int = WT_FFM_SUCCESS;
        break;
    case WT_SPM_OP_WRITE:
        ret = wt_spm_check_buffer(caller_domain, call->buffer, call->num_bytes,
                                  0);
        if (ret == WT_FFM_SUCCESS)
            ret = wt_ffm_write(runtime, call->partition_id, call->msg_handle,
                               call->vec_idx, call->buffer, call->num_bytes);
        call->ret_int = ret;
        break;
    case WT_SPM_OP_REPLY:
        call->ret_int = wt_ffm_reply(runtime, call->partition_id,
                                     call->msg_handle, call->status);
        break;
    case WT_SPM_OP_NOTIFY:
        call->ret_int = wt_ffm_notify(runtime, call->notify_partition);
        break;
    case WT_SPM_OP_CLEAR:
        call->ret_int = wt_ffm_clear(runtime, call->partition_id);
        break;
    case WT_SPM_OP_VERSION:
        call->ret_version = wt_ffm_service_version(runtime,
                                                   call->partition_id,
                                                   call->sid);
        call->ret_int = WT_FFM_SUCCESS;
        break;
    case WT_SPM_OP_CONNECT:
        if (call->pending_valid == 0U) {
            psa_handle_t handle = wt_ffm_connect_begin(runtime,
                call->partition_id, call->sid, call->version,
                &call->pending_msg);
            if (handle <= 0) {
                call->ret_handle = handle;
                call->ret_int = WT_FFM_SUCCESS;
            } else {
                call->pending_valid = 1U;
                call->ret_int = WT_FFM_ERROR_NOT_READY;
            }
        } else if (wt_spm_pending_owned(runtime, call) == 0) {
            call->ret_int = WT_FFM_ERROR_ARGUMENT;
        } else if (!wt_ffm_msg_complete(runtime, call->pending_msg)) {
            call->ret_int = WT_FFM_ERROR_NOT_READY;
        } else {
            call->ret_handle = wt_ffm_connect_finish(runtime,
                                                     call->pending_msg);
            call->pending_valid = 0U;
            call->ret_int = WT_FFM_SUCCESS;
        }
        break;
    case WT_SPM_OP_CALL:
        if (call->pending_valid == 0U) {
            ret = wt_spm_check_sp_vectors(caller_domain, call);
            if (ret != WT_FFM_SUCCESS) {
                call->ret_status = PSA_ERROR_PROGRAMMER_ERROR;
                call->ret_int = WT_FFM_SUCCESS;
                break;
            }
            call->ret_status = wt_ffm_call_begin(runtime, call->partition_id,
                call->msg_handle, call->call_type, call->sp_in,
                call->sp_in_len, call->sp_out, call->sp_out_len,
                &call->pending_msg);
            if (call->ret_status != PSA_SUCCESS) {
                call->ret_int = WT_FFM_SUCCESS;
            } else {
                call->pending_valid = 1U;
                call->ret_int = WT_FFM_ERROR_NOT_READY;
            }
        } else if (wt_spm_pending_owned(runtime, call) == 0) {
            call->ret_int = WT_FFM_ERROR_ARGUMENT;
        } else if (!wt_ffm_msg_complete(runtime, call->pending_msg)) {
            call->ret_int = WT_FFM_ERROR_NOT_READY;
        } else {
            call->ret_status = wt_ffm_call_finish(runtime, call->pending_msg,
                                                  call->sp_out,
                                                  call->sp_out_len);
            call->pending_valid = 0U;
            call->ret_int = WT_FFM_SUCCESS;
        }
        break;
    case WT_SPM_OP_CLOSE:
        if (call->pending_valid == 0U) {
            ret = wt_ffm_close_begin(runtime, call->partition_id,
                                     call->msg_handle, &call->pending_msg);
            if (ret != WT_FFM_SUCCESS) {
                call->ret_int = ret;
            } else if (call->pending_msg == WT_FFM_QUEUE_NONE) {
                call->ret_int = WT_FFM_SUCCESS;
            } else {
                call->pending_valid = 1U;
                call->ret_int = WT_FFM_ERROR_NOT_READY;
            }
        } else if (wt_spm_pending_owned(runtime, call) == 0) {
            call->ret_int = WT_FFM_ERROR_ARGUMENT;
        } else if (!wt_ffm_msg_complete(runtime, call->pending_msg)) {
            call->ret_int = WT_FFM_ERROR_NOT_READY;
        } else {
            call->ret_int = wt_ffm_close_finish(runtime, call->pending_msg);
            call->pending_valid = 0U;
        }
        break;
    default:
        return WT_FFM_ERROR_ARGUMENT;
    }

    return WT_FFM_SUCCESS;
}

int wt_spm_call_would_block(const wt_spm_call_t* call)
{
    if (call == NULL)
        return 0;
    if (call->ret_int != WT_FFM_ERROR_NOT_READY)
        return 0;
    return call->op == WT_SPM_OP_WAIT || call->op == WT_SPM_OP_CONNECT ||
           call->op == WT_SPM_OP_CALL || call->op == WT_SPM_OP_CLOSE;
}

int wt_spm_transport_direct(wt_ffm_runtime_t* runtime, wt_spm_call_t* call)
{
    return wt_spm_gate(runtime, NULL, call);
}
