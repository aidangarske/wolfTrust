/* wolftrust_attestation_client.c
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

#include <string.h>

#include <psa/initial_attestation.h>
#include <wolftrust/attestation.h>
#include <wolftrust/ffm_veneer.h>

#define WT_ATTEST_ERROR_INVALID_ARGUMENT -3300
#define WT_ATTEST_ERROR_BUFFER_TOO_SMALL -3301
#define WT_ATTEST_ERROR_NOT_READY -3302

#define WT_SERVICE_ATTEST_SID     4096u
#define WT_SERVICE_ATTEST_VERSION 1u
#define WT_FFM_IPC_CALL           0     /* PSA_IPC_CALL */

extern int WolfTrust_Attest_GetTokenSize(size_t challengeSize,
                                         size_t* tokenSize);
extern int WolfTrust_Attest_GetPublicKey(uint8_t* publicKey,
    size_t publicKeyCapacity, size_t* publicKeySize);

extern int32_t WolfTrust_FFM_Connect(uint32_t sid, uint32_t version);
extern int32_t WolfTrust_FFM_Call(int32_t handle, int32_t type,
                                  wt_ffm_veneer_iovec_t* ns_iovec);
extern void WolfTrust_FFM_Close(int32_t handle);

static psa_status_t wt_attest_map_status(int status)
{
    psa_status_t ret;

    switch (status) {
        case 0:
            ret = PSA_SUCCESS;
            break;
        case WT_ATTEST_ERROR_INVALID_ARGUMENT:
            ret = PSA_ERROR_INVALID_ARGUMENT;
            break;
        case WT_ATTEST_ERROR_BUFFER_TOO_SMALL:
            ret = PSA_ERROR_BUFFER_TOO_SMALL;
            break;
        case WT_ATTEST_ERROR_NOT_READY:
            ret = PSA_ERROR_BAD_STATE;
            break;
        default:
            ret = PSA_ERROR_GENERIC_ERROR;
            break;
    }

    return ret;
}

psa_status_t psa_initial_attest_get_token(const uint8_t* authChallenge,
    size_t challengeSize, uint8_t* token, size_t tokenCapacity,
    size_t* tokenSize)
{
    wt_ffm_veneer_iovec_t iovec;
    size_t exactSize = 0u;
    int32_t handle;
    int32_t status;
    int rc;

    if ((tokenSize == NULL) || (token == NULL) || (tokenCapacity == 0u)) {
        if (tokenSize != NULL) {
            *tokenSize = 0u;
        }
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    *tokenSize = 0u;

    /* Route the attestation token through FF-M IPC (SERVICE_ATTEST) instead of
     * a direct veneer. The COSE_Sign1 token length is deterministic, so the
     * size query gives the exact transfer size for the output vector. */
    rc = WolfTrust_Attest_GetTokenSize(challengeSize, &exactSize);
    if (rc != 0) {
        return wt_attest_map_status(rc);
    }
    if (exactSize > tokenCapacity) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    handle = WolfTrust_FFM_Connect(WT_SERVICE_ATTEST_SID,
                                   WT_SERVICE_ATTEST_VERSION);
    if (handle < 0) {
        return PSA_ERROR_GENERIC_ERROR;
    }
    memset(&iovec, 0, sizeof(iovec));
    iovec.in[0].base = authChallenge;
    iovec.in[0].len = (uint32_t)challengeSize;
    iovec.out[0].base = token;
    iovec.out[0].len = (uint32_t)exactSize;
    iovec.in_count = 1u;
    iovec.out_count = 1u;
    status = WolfTrust_FFM_Call(handle, WT_FFM_IPC_CALL, &iovec);
    WolfTrust_FFM_Close(handle);
    if (status != 0) {
        return (psa_status_t)status;
    }

    *tokenSize = exactSize;
    return PSA_SUCCESS;
}

psa_status_t psa_initial_attest_get_token_size(size_t challengeSize,
    size_t* tokenSize)
{
    return wt_attest_map_status(WolfTrust_Attest_GetTokenSize(challengeSize,
                                                               tokenSize));
}

psa_status_t wolftrust_attestation_get_iak_public_key(uint8_t* publicKey,
    size_t publicKeyCapacity, size_t* publicKeySize)
{
    return wt_attest_map_status(WolfTrust_Attest_GetPublicKey(publicKey,
        publicKeyCapacity, publicKeySize));
}
