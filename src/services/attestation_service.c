/* attestation_service.c
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

#include "wolftrust/services/attestation_service.h"
#include "wolftrust/services/initial_attestation.h"

static int wt_attestation_service_token(wt_ffm_runtime_t* runtime,
                                        int32_t partition_id,
                                        const psa_msg_t* msg)
{
    uint8_t challenge[WT_ATTEST_CHALLENGE_SIZE_64];
    uint8_t token[WT_ATTEST_MAX_TOKEN_SIZE];
    size_t challenge_len = 0U;
    size_t token_len = 0U;
    size_t got;
    wt_guest_id_t guest_id;
    int ret;

    /* Initial Attestation is a Non-secure client service; the token binds the
     * caller's guest identity, derived from the PSA client id. */
    if (msg->client_id >= 0) {
        return WT_FFM_ERROR_STATE;
    }
    guest_id = (wt_guest_id_t)(-msg->client_id - 1);

    if (msg->in_size[0] > sizeof(challenge)) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    for (;;) {
        got = wt_ffm_read(runtime, partition_id, msg->handle, 0U,
                          challenge + challenge_len,
                          sizeof(challenge) - challenge_len);
        if (got == 0U) {
            break;
        }
        challenge_len += got;
    }

    ret = wt_initial_attest_get_token(guest_id, challenge, challenge_len,
                                      token, sizeof(token), &token_len);
    if (ret != WT_ATTEST_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    if (wt_ffm_write(runtime, partition_id, msg->handle, 0U, token,
                     token_len) != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

int wt_attestation_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
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
        reply_status = wt_attestation_service_token(runtime, partition_id,
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
