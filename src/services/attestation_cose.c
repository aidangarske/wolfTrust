/* attestation_cose.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 */

#include "wolftrust/services/attestation_cose.h"

#include <wolfcose/wolfcose.h>

typedef struct wt_attest_cose_bridge {
    const wt_attest_cose_signer_t* signer;
} wt_attest_cose_bridge_t;

static int wt_attest_cose_validate_signer(
    const wt_attest_cose_signer_t* signer)
{
    if ((signer == NULL) || (signer->sign == NULL)) {
        return WT_ATTEST_COSE_E_BADARG;
    }
    if (((signer->keyId == NULL) && (signer->keyIdSize != 0u)) ||
        ((signer->keyId != NULL) && (signer->keyIdSize == 0u))) {
        return WT_ATTEST_COSE_E_BADARG;
    }

    return WT_ATTEST_COSE_OK;
}

static int wt_attest_cose_flags(uint32_t flags, uint32_t* wolfCoseFlags)
{
    if ((wolfCoseFlags == NULL) ||
        ((flags & ~WT_ATTEST_COSE_FLAG_UNTAGGED) != 0u)) {
        return WT_ATTEST_COSE_E_BADARG;
    }

    *wolfCoseFlags = 0u;
    if ((flags & WT_ATTEST_COSE_FLAG_UNTAGGED) != 0u) {
        *wolfCoseFlags = WOLFCOSE_SIGN1_UNTAGGED;
    }

    return WT_ATTEST_COSE_OK;
}

static int wt_attest_cose_map_error(int wolfCoseError)
{
    int ret;

    switch (wolfCoseError) {
        case WOLFCOSE_SUCCESS:
            ret = WT_ATTEST_COSE_OK;
            break;
        case WOLFCOSE_E_INVALID_ARG:
            ret = WT_ATTEST_COSE_E_BADARG;
            break;
        case WOLFCOSE_E_BUFFER_TOO_SMALL:
            ret = WT_ATTEST_COSE_E_BUFFER;
            break;
        case WOLFCOSE_E_CRYPTO:
            ret = WT_ATTEST_COSE_E_SIGN;
            break;
        default:
            ret = WT_ATTEST_COSE_E_ENCODE;
            break;
    }

    return ret;
}

static int wt_attest_cose_sign_bridge(void* context, int32_t algorithm,
    const uint8_t* digest, size_t digestSize, uint8_t* signature,
    size_t signatureSize, size_t* signatureLength)
{
    wt_attest_cose_bridge_t* bridge =
        (wt_attest_cose_bridge_t*)context;

    if ((bridge == NULL) || (bridge->signer == NULL) ||
        (bridge->signer->sign == NULL) ||
        (algorithm != WOLFCOSE_ALG_ES256)) {
        return -1;
    }

    return bridge->signer->sign(bridge->signer->context, digest,
        digestSize, signature, signatureSize, signatureLength);
}

int wt_attest_cose_sign1_size(const wt_attest_cose_signer_t* signer,
    size_t payloadSize, uint32_t flags, size_t* tokenSize)
{
    uint32_t wolfCoseFlags = 0u;
    int ret;

    if (tokenSize == NULL) {
        return WT_ATTEST_COSE_E_BADARG;
    }
    *tokenSize = 0u;

    ret = wt_attest_cose_validate_signer(signer);
    if ((ret == WT_ATTEST_COSE_OK) && (payloadSize == 0u)) {
        ret = WT_ATTEST_COSE_E_BADARG;
    }
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_attest_cose_flags(flags, &wolfCoseFlags);
    }
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_attest_cose_map_error(wc_CoseSign1_SignSize_ex(NULL,
            WOLFCOSE_ALG_ES256, signer->keyIdSize, payloadSize, 0u,
            wolfCoseFlags, tokenSize));
    }

    return ret;
}

int wt_attest_cose_sign1_encode(const wt_attest_cose_signer_t* signer,
    const uint8_t* payload, size_t payloadSize, uint32_t flags,
    uint8_t* scratch, size_t scratchSize, uint8_t* token,
    size_t tokenCapacity, size_t* tokenSize)
{
    wt_attest_cose_bridge_t bridge;
    WOLFCOSE_KEY key;
    uint32_t wolfCoseFlags = 0u;
    int keyInited = 0;
    int ret;

    if (tokenSize == NULL) {
        return WT_ATTEST_COSE_E_BADARG;
    }
    *tokenSize = 0u;

    ret = wt_attest_cose_validate_signer(signer);
    if ((ret == WT_ATTEST_COSE_OK) &&
        ((payload == NULL) || (payloadSize == 0u) || (scratch == NULL) ||
         (scratchSize == 0u) || (token == NULL) ||
         (tokenCapacity == 0u))) {
        ret = WT_ATTEST_COSE_E_BADARG;
    }
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_attest_cose_flags(flags, &wolfCoseFlags);
    }
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_attest_cose_map_error(wc_CoseKey_Init(&key));
        if (ret == WT_ATTEST_COSE_OK) {
            keyInited = 1;
        }
    }
    if (ret == WT_ATTEST_COSE_OK) {
        key.kty = WOLFCOSE_KTY_EC2;
        key.crv = WOLFCOSE_CRV_P256;
        key.alg = WOLFCOSE_ALG_ES256;
        bridge.signer = signer;
        ret = wt_attest_cose_map_error(wc_CoseKey_SetExtSigner(&key,
            wt_attest_cose_sign_bridge, &bridge));
    }
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_attest_cose_map_error(wc_CoseSign1_Sign_ex(&key,
            WOLFCOSE_ALG_ES256, signer->keyId, signer->keyIdSize,
            payload, payloadSize, NULL, 0u, NULL, 0u, scratch,
            scratchSize, token, tokenCapacity, tokenSize, NULL,
            wolfCoseFlags));
    }

    if (keyInited != 0) {
        wc_CoseKey_Free(&key);
    }

    return ret;
}
