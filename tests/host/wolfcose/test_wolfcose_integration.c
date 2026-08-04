/* test_wolfcose_integration.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 */

#include <stdio.h>
#include <string.h>

#include <wolfcose/wolfcose.h>

#define WT_TEST_ES256_DIGEST_SIZE    32u
#define WT_TEST_ES256_SIGNATURE_SIZE 64u

typedef struct wt_test_signer_ctx {
    unsigned int calls;
} wt_test_signer_ctx_t;

static int wt_test_external_sign(void* signerCtx, int32_t alg,
    const uint8_t* digest, size_t digestSize, uint8_t* signature,
    size_t signatureSize, size_t* signatureLength)
{
    wt_test_signer_ctx_t* ctx = (wt_test_signer_ctx_t*)signerCtx;

    if ((ctx == NULL) || (digest == NULL) || (signature == NULL) ||
        (signatureLength == NULL) || (alg != WOLFCOSE_ALG_ES256) ||
        (digestSize != WT_TEST_ES256_DIGEST_SIZE) ||
        (signatureSize < WT_TEST_ES256_SIGNATURE_SIZE)) {
        return -1;
    }

    (void)digest;
    (void)memset(signature, 0xA5, WT_TEST_ES256_SIGNATURE_SIZE);
    *signatureLength = WT_TEST_ES256_SIGNATURE_SIZE;
    ctx->calls++;

    return 0;
}

static int wt_test_encode_payload(uint8_t* payload, size_t payloadSize,
    size_t* payloadLength)
{
    static const uint8_t component[] = "wolfTrust";
    static const uint8_t measurement[] = {
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE
    };
    WOLFCOSE_CBOR_CTX cbor;
    int ret;

    if ((payload == NULL) || (payloadLength == NULL)) {
        return WOLFCOSE_E_INVALID_ARG;
    }

    cbor.buf = payload;
    cbor.cbuf = NULL;
    cbor.bufSz = payloadSize;
    cbor.idx = 0u;

    ret = wc_CBOR_EncodeMapStart(&cbor, 2u);
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wc_CBOR_EncodeUint(&cbor, 1u);
    }
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wc_CBOR_EncodeTstr(&cbor, component,
            sizeof(component) - 1u);
    }
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wc_CBOR_EncodeUint(&cbor, 2u);
    }
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wc_CBOR_EncodeBstr(&cbor, measurement,
            sizeof(measurement));
    }
    if (ret == WOLFCOSE_SUCCESS) {
        *payloadLength = cbor.idx;
    }

    return ret;
}

static int wt_test_sign_output(WOLFCOSE_KEY* key, const uint8_t* payload,
    size_t payloadLength, uint32_t flags, uint8_t expectedFirstByte,
    size_t* outputLength)
{
    uint8_t scratch[256];
    uint8_t output[256];
    size_t predictedLength = 0u;
    int ret;

    ret = wc_CoseSign1_SignSize_ex(key, WOLFCOSE_ALG_ES256, 0u,
        payloadLength, 0u, flags, &predictedLength);
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wc_CoseSign1_Sign_ex(key, WOLFCOSE_ALG_ES256, NULL, 0u,
            payload, payloadLength, NULL, 0u, NULL, 0u, scratch,
            sizeof(scratch), output, sizeof(output), outputLength, NULL,
            flags);
    }
    if ((ret == WOLFCOSE_SUCCESS) &&
        ((*outputLength != predictedLength) ||
         (output[0] != expectedFirstByte))) {
        ret = -1;
    }

    return ret;
}

int main(void)
{
    uint8_t payload[64];
    WOLFCOSE_KEY key;
    wt_test_signer_ctx_t signerCtx;
    size_t payloadLength = 0u;
    size_t taggedLength = 0u;
    size_t untaggedLength = 0u;
    int keyInited = 0;
    int ret;

    (void)memset(&signerCtx, 0, sizeof(signerCtx));

    ret = wt_test_encode_payload(payload, sizeof(payload), &payloadLength);
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wc_CoseKey_Init(&key);
        if (ret == WOLFCOSE_SUCCESS) {
            keyInited = 1;
        }
    }
    if (ret == WOLFCOSE_SUCCESS) {
        key.kty = WOLFCOSE_KTY_EC2;
        key.crv = WOLFCOSE_CRV_P256;
        key.alg = WOLFCOSE_ALG_ES256;
        ret = wc_CoseKey_SetExtSigner(&key, wt_test_external_sign,
            &signerCtx);
    }
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wt_test_sign_output(&key, payload, payloadLength, 0u,
            0xD2u, &taggedLength);
    }
    if (ret == WOLFCOSE_SUCCESS) {
        ret = wt_test_sign_output(&key, payload, payloadLength,
            WOLFCOSE_SIGN1_UNTAGGED, 0x84u, &untaggedLength);
    }
    if ((ret == WOLFCOSE_SUCCESS) &&
        ((taggedLength != (untaggedLength + 1u)) ||
         (signerCtx.calls != 2u))) {
        ret = -1;
    }

    if (keyInited != 0) {
        wc_CoseKey_Free(&key);
    }

    if (ret != WOLFCOSE_SUCCESS) {
        (void)fprintf(stderr, "wolfTrust wolfCOSE integration failed: %d\n",
            ret);
        return 1;
    }

    (void)printf("wolfTrust wolfCOSE integration passed\n");
    return 0;
}
