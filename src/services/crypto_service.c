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
    ret = wc_InitSha256(&sha);
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

static int wt_crypto_service_hash(wt_ffm_runtime_t* runtime,
                                  int32_t partition_id,
                                  const psa_msg_t* msg)
{
    uint8_t input[WT_CRYPTO_SP_INPUT_MAX];
    uint8_t digest[WC_SHA256_DIGEST_SIZE];
    size_t in_len = 0U;
    size_t got;
    int ret;

    /* Copied IOVEC (WT-FFM-0041): the SPM drains the input vector into a
     * bounded private buffer, refusing anything past the SP input cap, so the
     * isolated compute never reads caller memory. */
    if (msg->in_size[0] > sizeof(input)) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    for (;;) {
        got = wt_ffm_read(runtime, partition_id, msg->handle, 0U,
                          input + in_len, sizeof(input) - in_len);
        if (got == 0U) {
            break;
        }
        in_len += got;
    }

    ret = wt_crypto_sp_hash(input, in_len, digest, sizeof(digest));
    if (ret != WT_FFM_SUCCESS) {
        return ret;
    }
    if (wt_ffm_write(runtime, partition_id, msg->handle, 0U, digest,
                     sizeof(digest)) != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

int wt_crypto_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                               int32_t partition_id)
{
    psa_signal_t asserted;
    psa_msg_t msg;
    psa_status_t reply_status;

    (void)context;
    if (wt_ffm_wait(runtime, partition_id, PSA_WAIT_ANY, &asserted) !=
            WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    if (wt_ffm_get(runtime, partition_id, asserted, &msg) != PSA_SUCCESS) {
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

    if (wt_ffm_reply(runtime, partition_id, msg.handle, reply_status) !=
            WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}
