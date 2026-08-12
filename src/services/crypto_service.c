/* crypto_service.c
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

#include "wolftrust/services/crypto_service.h"
#include "wolftrust/spm_gate.h"

#include <string.h>

#include <wolfssl/wolfcrypt/sha256.h>

int wt_crypto_sp_hash(const uint8_t* input, size_t input_len,
                      uint8_t* digest, size_t digest_len)
{
    wc_Sha256 sha;
    int ret;

    if ((input == NULL && input_len != 0U) || digest == NULL ||
            digest_len < WC_SHA256_DIGEST_SIZE) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    /* INVALID_DEVID keeps the isolated SP compute pure software: with
     * WOLF_CRYPTO_CB the default devId routes SHA through the shared crypto
     * callback registry (wolfHSM), which lies outside the SP's MPU domain. */
    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret == 0) {
        ret = wc_Sha256Update(&sha, input, (word32)input_len);
    }
    if (ret == 0) {
        ret = wc_Sha256Final(&sha, digest);
    }
    if (ret != 0) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

/* Compute seam: the host default runs wt_crypto_sp_hash inline; the production
 * port installs an isolated runner (own SP stack + narrowed MPU) at boot. */
static wt_crypto_sp_compute_fn g_crypto_sp_compute = wt_crypto_sp_hash;

void wt_crypto_service_set_compute(wt_crypto_sp_compute_fn fn)
{
    g_crypto_sp_compute = (fn != NULL) ? fn : wt_crypto_sp_hash;
}

static int wt_crypto_service_hash(wt_ffm_runtime_t* runtime,
                                  int32_t partition_id,
                                  const psa_msg_t* msg)
{
    uint8_t input[WT_CRYPTO_SP_INPUT_MAX];
    uint8_t digest[WC_SHA256_DIGEST_SIZE];
    wt_spm_call_t call;
    size_t in_len = 0U;
    int ret;

    /* Copied IOVEC (WT-FFM-0041): the SPM drains the input vector into a
     * bounded private buffer, refusing anything past the SP input cap, so the
     * isolated compute never reads caller memory. */
    if (msg->in_size[0] > sizeof(input)) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    for (;;) {
        (void)memset(&call, 0, sizeof(call));
        call.op = WT_SPM_OP_READ;
        call.partition_id = partition_id;
        call.msg_handle = msg->handle;
        call.buffer = input + in_len;
        call.num_bytes = sizeof(input) - in_len;
        if (wt_spm_gate(runtime, NULL, &call) != WT_FFM_SUCCESS) {
            return WT_FFM_ERROR_STATE;
        }
        if (call.ret_size == 0U) {
            break;
        }
        in_len += call.ret_size;
    }

    ret = g_crypto_sp_compute(input, in_len, digest, sizeof(digest));
    if (ret != WT_FFM_SUCCESS) {
        return ret;
    }
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WRITE;
    call.partition_id = partition_id;
    call.msg_handle = msg->handle;
    call.buffer = digest;
    call.num_bytes = sizeof(digest);
    if (wt_spm_gate(runtime, NULL, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

int wt_crypto_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                               int32_t partition_id)
{
    psa_signal_t asserted = 0U;
    psa_msg_t msg;
    psa_status_t reply_status;
    wt_spm_call_t call;

    (void)context;
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = partition_id;
    call.signal_mask = PSA_WAIT_ANY;
    call.asserted = &asserted;
    if (wt_spm_gate(runtime, NULL, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_GET;
    call.partition_id = partition_id;
    call.signal = asserted;
    call.msg = &msg;
    if (wt_spm_gate(runtime, NULL, &call) != WT_FFM_SUCCESS ||
            call.ret_status != PSA_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }

    if (msg.type == PSA_IPC_CONNECT || msg.type == PSA_IPC_DISCONNECT) {
        reply_status = PSA_SUCCESS;
    } else if (msg.type == PSA_IPC_CALL) {
        reply_status = wt_crypto_service_hash(runtime, partition_id,
                           &msg) == WT_FFM_SUCCESS ?
                       PSA_SUCCESS : PSA_ERROR_GENERIC_ERROR;
    } else {
        reply_status = PSA_ERROR_NOT_SUPPORTED;
    }

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_REPLY;
    call.partition_id = partition_id;
    call.msg_handle = msg.handle;
    call.status = reply_status;
    if (wt_spm_gate(runtime, NULL, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}
