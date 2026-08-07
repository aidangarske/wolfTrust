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

static int wt_crypto_service_hash(wt_ffm_runtime_t* runtime,
                                  int32_t partition_id,
                                  psa_handle_t msg_handle)
{
    wc_Sha256 sha;
    uint8_t chunk[64];
    uint8_t digest[WC_SHA256_DIGEST_SIZE];
    size_t got;

    if (wc_InitSha256(&sha) != 0) {
        return WT_FFM_ERROR_STATE;
    }
    for (;;) {
        got = wt_ffm_read(runtime, partition_id, msg_handle, 0U, chunk,
                          sizeof(chunk));
        if (got == 0U) {
            break;
        }
        if (wc_Sha256Update(&sha, chunk, (word32)got) != 0) {
            return WT_FFM_ERROR_STATE;
        }
    }
    if (wc_Sha256Final(&sha, digest) != 0) {
        return WT_FFM_ERROR_STATE;
    }
    if (wt_ffm_write(runtime, partition_id, msg_handle, 0U, digest,
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
                           msg.handle) == WT_FFM_SUCCESS ?
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
