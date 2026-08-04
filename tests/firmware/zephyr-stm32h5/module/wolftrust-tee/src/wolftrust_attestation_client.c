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

#include <psa/initial_attestation.h>
#include <wolftrust/attestation.h>

#define WT_ATTEST_ERROR_INVALID_ARGUMENT -3300
#define WT_ATTEST_ERROR_BUFFER_TOO_SMALL -3301
#define WT_ATTEST_ERROR_NOT_READY -3302

extern int WolfTrust_Attest_GetTokenSize(size_t challengeSize,
                                         size_t* tokenSize);
extern int WolfTrust_Attest_GetToken(const uint8_t* challenge,
    size_t challengeSize, uint8_t* token, size_t* tokenSize);
extern int WolfTrust_Attest_GetPublicKey(uint8_t* publicKey,
    size_t publicKeyCapacity, size_t* publicKeySize);

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
    if (tokenSize == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    *tokenSize = tokenCapacity;
    return wt_attest_map_status(WolfTrust_Attest_GetToken(authChallenge,
        challengeSize, token, tokenSize));
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
