/* main.c
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

/*
 * Phase 5 buckets 2 and 3 (COSE layer): drive the production
 * wt_attest_cose_* seam with a real ES256 signer, verify the COSE_Sign1
 * token against the IAK public key with wolfCOSE, and prove that tampered,
 * truncated, wrong-key, and malformed inputs are rejected. The full EAT
 * claim set and the HSM-backed IAK are covered separately on M33MU/H5.
 */

#include "wolftrust/services/attestation_cose.h"

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>

#include <wolfcose/wolfcose.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ES256_RAW_SIG_SIZE 64u
#define IAK_PUB_SIZE 65u

static int g_checks;
static int g_failures;

static void check(int cond, const char* name)
{
    g_checks++;
    if (cond != 0) {
        printf("  [check] PASS  %s\n", name);
    }
    else {
        g_failures++;
        printf("  [check] FAIL  %s\n", name);
    }
}

typedef struct signer_ctx {
    ecc_key* key;
    WC_RNG* rng;
} signer_ctx_t;

/* Produce a 64-byte raw r||s ES256 signature from a local P-256 key, the
 * same shape the wolfHSM-backed IAK signer returns on target. */
static int host_es256_sign(void* context, const uint8_t* digest,
    size_t digestSize, uint8_t* signature, size_t signatureSize,
    size_t* signatureLength)
{
    signer_ctx_t* ctx = (signer_ctx_t*)context;
    uint8_t der[80];
    uint8_t r[32];
    uint8_t s[32];
    word32 derLen = (word32)sizeof(der);
    word32 rLen = (word32)sizeof(r);
    word32 sLen = (word32)sizeof(s);
    int ret;

    if ((ctx == NULL) || (digest == NULL) || (signature == NULL) ||
        (signatureLength == NULL) || (digestSize != 32u) ||
        (signatureSize < ES256_RAW_SIG_SIZE)) {
        return -1;
    }
    ret = wc_ecc_sign_hash(digest, (word32)digestSize, der, &derLen,
                           ctx->rng, ctx->key);
    if (ret == 0) {
        ret = wc_ecc_sig_to_rs(der, derLen, r, &rLen, s, &sLen);
    }
    if (ret == 0) {
        (void)memset(signature, 0, ES256_RAW_SIG_SIZE);
        (void)memcpy(signature + (32u - rLen), r, rLen);
        (void)memcpy(signature + 32u + (32u - sLen), s, sLen);
        *signatureLength = ES256_RAW_SIG_SIZE;
    }
    return ret == 0 ? 0 : -1;
}

/* Verify a COSE_Sign1 token against a 65-byte uncompressed IAK public key
 * and copy the recovered payload out. Returns 0 only on ES256 verify. */
static int cose_verify(const uint8_t* pub, size_t pubSize,
    const uint8_t* token, size_t tokenSize, uint8_t* payloadBuf,
    size_t payloadCap, size_t* payloadLen)
{
    uint8_t scratch[512];
    WOLFCOSE_HDR header;
    WOLFCOSE_KEY coseKey;
    ecc_key eccKey;
    const uint8_t* payload = NULL;
    size_t payloadSize = 0u;
    int coseInited = 0;
    int eccInited = 0;
    int ret;

    if ((pub == NULL) || (pubSize != IAK_PUB_SIZE) || (pub[0] != 0x04u)) {
        return -1;
    }
    ret = wc_ecc_init(&eccKey);
    if (ret == 0) {
        eccInited = 1;
        ret = wc_ecc_import_unsigned(&eccKey, &pub[1], &pub[33], NULL,
                                     ECC_SECP256R1);
    }
    if (ret == 0) {
        ret = wc_CoseKey_Init(&coseKey);
        if (ret == 0) {
            coseInited = 1;
            ret = wc_CoseKey_SetEcc(&coseKey, WOLFCOSE_CRV_P256, &eccKey);
        }
    }
    if (ret == 0) {
        (void)memset(&header, 0, sizeof(header));
        ret = wc_CoseSign1_Verify(&coseKey, token, tokenSize, NULL, 0u,
            NULL, 0u, scratch, sizeof(scratch), &header, &payload,
            &payloadSize);
    }
    if ((ret == 0) && (header.alg != WOLFCOSE_ALG_ES256)) {
        ret = -1;
    }
    if ((ret == 0) && (payloadBuf != NULL) && (payloadLen != NULL)) {
        if (payloadSize > payloadCap) {
            ret = -1;
        }
        else {
            (void)memcpy(payloadBuf, payload, payloadSize);
            *payloadLen = payloadSize;
        }
    }

    if (coseInited != 0) {
        wc_CoseKey_Free(&coseKey);
    }
    if (eccInited != 0) {
        wc_ecc_free(&eccKey);
    }
    (void)memset(scratch, 0, sizeof(scratch));
    return ret;
}

int main(void)
{
    WC_RNG rng;
    ecc_key key;
    ecc_key wrongKey;
    signer_ctx_t sctx;
    wt_attest_cose_signer_t signer;
    uint8_t pub[IAK_PUB_SIZE];
    uint8_t wrongPub[IAK_PUB_SIZE];
    uint8_t payload[40];
    uint8_t token[256];
    uint8_t scratch[512];
    uint8_t verified[64];
    word32 pubLen = (word32)sizeof(pub);
    word32 wrongPubLen = (word32)sizeof(wrongPub);
    size_t tokenSize = 0u;
    size_t predicted = 0u;
    size_t verifiedLen = 0u;
    int ret;
    int i;

    if (wc_InitRng(&rng) != 0) {
        printf("FAIL: attestation (RNG init)\n");
        return 1;
    }
    if ((wc_ecc_init(&key) != 0) || (wc_ecc_init(&wrongKey) != 0)) {
        printf("FAIL: attestation (ecc init)\n");
        return 1;
    }
    if ((wc_ecc_make_key(&rng, 32, &key) != 0) ||
        (wc_ecc_make_key(&rng, 32, &wrongKey) != 0)) {
        printf("FAIL: attestation (keygen)\n");
        return 1;
    }
    if ((wc_ecc_export_x963(&key, pub, &pubLen) != 0) ||
        (wc_ecc_export_x963(&wrongKey, wrongPub, &wrongPubLen) != 0)) {
        printf("FAIL: attestation (pubkey export)\n");
        return 1;
    }
    check((pubLen == IAK_PUB_SIZE) && (pub[0] == 0x04u),
          "IAK public key is a 65-byte uncompressed P-256 point");

    for (i = 0; i < (int)sizeof(payload); i++) {
        payload[i] = (uint8_t)(0x40 + i);
    }

    (void)memset(&signer, 0, sizeof(signer));
    sctx.key = &key;
    sctx.rng = &rng;
    signer.sign = host_es256_sign;
    signer.context = &sctx;

    /* Bucket 2: predicted size == encoded size (untagged, production parity). */
    ret = wt_attest_cose_sign1_size(&signer, sizeof(payload),
        WT_ATTEST_COSE_FLAG_UNTAGGED, &predicted);
    check((ret == WT_ATTEST_COSE_OK) && (predicted > 0u),
          "sign1_size predicts a token size");

    ret = wt_attest_cose_sign1_encode(&signer, payload, sizeof(payload),
        WT_ATTEST_COSE_FLAG_UNTAGGED, scratch, sizeof(scratch), token,
        sizeof(token), &tokenSize);
    check(ret == WT_ATTEST_COSE_OK, "sign1_encode signs the payload");
    check(tokenSize == predicted, "encoded size matches predicted size");
    check((tokenSize > 0u) && (token[0] == 0x84u),
          "untagged COSE_Sign1 is a 4-element array (0x84)");

    /* Bucket 2: verify the real ES256 signature against the IAK public key. */
    ret = cose_verify(pub, sizeof(pub), token, tokenSize, verified,
                      sizeof(verified), &verifiedLen);
    check(ret == 0, "COSE_Sign1 verifies against the IAK public key (ES256)");
    check((verifiedLen == sizeof(payload)) &&
          (memcmp(verified, payload, sizeof(payload)) == 0),
          "verified payload matches the signed claims");

    /* Bucket 3: tampered signature byte -> reject. */
    token[tokenSize - 1u] ^= 0x01u;
    ret = cose_verify(pub, sizeof(pub), token, tokenSize, NULL, 0u, NULL);
    check(ret != 0, "tampered signature is rejected");
    token[tokenSize - 1u] ^= 0x01u;

    /* Bucket 3: tampered token body -> reject. */
    token[tokenSize / 2u] ^= 0x01u;
    ret = cose_verify(pub, sizeof(pub), token, tokenSize, NULL, 0u, NULL);
    check(ret != 0, "tampered token body is rejected");
    token[tokenSize / 2u] ^= 0x01u;

    /* Bucket 3: truncated token -> reject. */
    ret = cose_verify(pub, sizeof(pub), token, tokenSize - 1u, NULL, 0u, NULL);
    check(ret != 0, "truncated token is rejected");

    /* Bucket 3: verify with the wrong public key -> reject. */
    ret = cose_verify(wrongPub, sizeof(wrongPub), token, tokenSize, NULL, 0u,
                      NULL);
    check(ret != 0, "verification with the wrong public key fails");

    /* Bucket 3: production seam input validation. */
    ret = wt_attest_cose_sign1_encode(&signer, payload, sizeof(payload),
        WT_ATTEST_COSE_FLAG_UNTAGGED, scratch, sizeof(scratch), token, 8u,
        &tokenSize);
    check(ret == WT_ATTEST_COSE_E_BUFFER,
          "encode into an undersized token buffer is rejected");

    ret = wt_attest_cose_sign1_encode(&signer, payload, sizeof(payload),
        0x10u, scratch, sizeof(scratch), token, sizeof(token), &tokenSize);
    check(ret == WT_ATTEST_COSE_E_BADARG, "unknown flags are rejected");

    ret = wt_attest_cose_sign1_encode(NULL, payload, sizeof(payload),
        WT_ATTEST_COSE_FLAG_UNTAGGED, scratch, sizeof(scratch), token,
        sizeof(token), &tokenSize);
    check(ret == WT_ATTEST_COSE_E_BADARG, "null signer is rejected");

    /* Bucket 2: a tagged token carries the COSE_Sign1 tag 18 (0xD2). */
    ret = wt_attest_cose_sign1_encode(&signer, payload, sizeof(payload), 0u,
        scratch, sizeof(scratch), token, sizeof(token), &tokenSize);
    check((ret == WT_ATTEST_COSE_OK) && (token[0] == 0xD2u),
          "tagged COSE_Sign1 carries tag 18 (0xD2)");

    wc_ecc_free(&key);
    wc_ecc_free(&wrongKey);
    wc_FreeRng(&rng);

    printf("attestation host tests: %d checks, %d failures\n", g_checks,
           g_failures);
    if (g_failures == 0) {
        printf("PASS: attestation\n");
        return 0;
    }
    printf("FAIL: attestation\n");
    return 1;
}
