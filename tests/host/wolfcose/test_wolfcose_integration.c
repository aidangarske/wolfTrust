/* test_wolfcose_integration.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 */

#include <stdio.h>
#include <string.h>

#include "wolftrust/services/attestation_cose.h"

#define WT_TEST_ES256_DIGEST_SIZE    32u
#define WT_TEST_ES256_SIGNATURE_SIZE 64u

typedef struct wt_test_signer_context {
    unsigned int calls;
    int fail;
} wt_test_signer_context_t;

static int wt_test_external_sign(void* context, const uint8_t* digest,
    size_t digestSize, uint8_t* signature, size_t signatureSize,
    size_t* signatureLength)
{
    wt_test_signer_context_t* signer =
        (wt_test_signer_context_t*)context;

    if ((signer == NULL) || (digest == NULL) || (signature == NULL) ||
        (signatureLength == NULL) ||
        (digestSize != WT_TEST_ES256_DIGEST_SIZE) ||
        (signatureSize < WT_TEST_ES256_SIGNATURE_SIZE) ||
        (signer->fail != 0)) {
        return -1;
    }

    (void)memset(signature, 0xA5, WT_TEST_ES256_SIGNATURE_SIZE);
    *signatureLength = WT_TEST_ES256_SIGNATURE_SIZE;
    signer->calls++;

    return 0;
}

static int wt_test_all_zero(const uint8_t* data, size_t dataSize)
{
    size_t i;

    for (i = 0u; i < dataSize; i++) {
        if (data[i] != 0u) {
            return 0;
        }
    }

    return 1;
}

static int wt_test_sign_output(const wt_attest_cose_signer_t* signer,
    const uint8_t* payload, size_t payloadSize, uint32_t flags,
    uint8_t expectedFirstByte, size_t* outputSize)
{
    uint8_t scratch[256];
    uint8_t output[256];
    size_t predictedSize = 0u;
    int ret;

    (void)memset(scratch, 0x3C, sizeof(scratch));
    ret = wt_attest_cose_sign1_size(signer, payloadSize, flags,
        &predictedSize);
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_attest_cose_sign1_encode(signer, payload, payloadSize,
            flags, scratch, sizeof(scratch), output, sizeof(output),
            outputSize);
    }
    if ((ret == WT_ATTEST_COSE_OK) &&
        ((*outputSize != predictedSize) ||
         (output[0] != expectedFirstByte) ||
         (wt_test_all_zero(scratch, sizeof(scratch)) == 0))) {
        ret = WT_ATTEST_COSE_E_ENCODE;
    }

    return ret;
}

static int wt_test_invalid_inputs(wt_attest_cose_signer_t* signer,
    const uint8_t* payload, size_t payloadSize)
{
    uint8_t scratch[256];
    uint8_t output[256];
    size_t outputSize = 9u;
    int ret = WT_ATTEST_COSE_OK;

    if (wt_attest_cose_sign1_size(NULL, payloadSize, 0u,
            &outputSize) != WT_ATTEST_COSE_E_BADARG) {
        ret = WT_ATTEST_COSE_E_ENCODE;
    }
    if ((ret == WT_ATTEST_COSE_OK) &&
        (wt_attest_cose_sign1_size(signer, 0u, 0u,
            &outputSize) != WT_ATTEST_COSE_E_BADARG)) {
        ret = WT_ATTEST_COSE_E_ENCODE;
    }
    if ((ret == WT_ATTEST_COSE_OK) &&
        (wt_attest_cose_sign1_size(signer, payloadSize, 0x80000000u,
            &outputSize) != WT_ATTEST_COSE_E_BADARG)) {
        ret = WT_ATTEST_COSE_E_ENCODE;
    }
    if ((ret == WT_ATTEST_COSE_OK) &&
        (wt_attest_cose_sign1_encode(signer, payload, payloadSize, 0u,
            scratch, sizeof(scratch), output, 1u,
            &outputSize) != WT_ATTEST_COSE_E_BUFFER)) {
        ret = WT_ATTEST_COSE_E_ENCODE;
    }

    return ret;
}

static int wt_test_signer_failure(wt_attest_cose_signer_t* signer,
    wt_test_signer_context_t* context, const uint8_t* payload,
    size_t payloadSize)
{
    uint8_t scratch[256];
    uint8_t output[256];
    size_t outputSize = 9u;
    int ret;

    (void)memset(output, 0x5A, sizeof(output));
    context->fail = 1;
    ret = wt_attest_cose_sign1_encode(signer, payload, payloadSize, 0u,
        scratch, sizeof(scratch), output, sizeof(output), &outputSize);
    context->fail = 0;

    if ((ret != WT_ATTEST_COSE_E_SIGN) || (outputSize != 0u) ||
        (wt_test_all_zero(output, sizeof(output)) == 0)) {
        return WT_ATTEST_COSE_E_ENCODE;
    }

    return WT_ATTEST_COSE_OK;
}

int main(void)
{
    static const uint8_t payload[] = {
        0xA2, 0x01, 0x69, 0x77, 0x6F, 0x6C, 0x66, 0x54, 0x72, 0x75,
        0x73, 0x74, 0x02, 0x48, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA,
        0xDC, 0xFE
    };
    static const uint8_t keyId[] = {0x01, 0x02, 0x03, 0x04};
    wt_test_signer_context_t context;
    wt_attest_cose_signer_t signer;
    size_t taggedSize = 0u;
    size_t untaggedSize = 0u;
    int ret;

    (void)memset(&context, 0, sizeof(context));
    signer.sign = wt_test_external_sign;
    signer.context = &context;
    signer.keyId = keyId;
    signer.keyIdSize = sizeof(keyId);

    ret = wt_test_sign_output(&signer, payload, sizeof(payload), 0u,
        0xD2u, &taggedSize);
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_test_sign_output(&signer, payload, sizeof(payload),
            WT_ATTEST_COSE_FLAG_UNTAGGED, 0x84u, &untaggedSize);
    }
    if ((ret == WT_ATTEST_COSE_OK) &&
        ((taggedSize != (untaggedSize + 1u)) ||
         (context.calls != 2u))) {
        ret = WT_ATTEST_COSE_E_ENCODE;
    }
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_test_invalid_inputs(&signer, payload, sizeof(payload));
    }
    if (ret == WT_ATTEST_COSE_OK) {
        ret = wt_test_signer_failure(&signer, &context, payload,
            sizeof(payload));
    }

    if (ret != WT_ATTEST_COSE_OK) {
        (void)fprintf(stderr, "wolfTrust wolfCOSE integration failed: %d\n",
            ret);
        return 1;
    }

    (void)printf("wolfTrust wolfCOSE production integration passed\n");
    return 0;
}
