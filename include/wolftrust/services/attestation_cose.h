/* attestation_cose.h
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 */

#ifndef WOLFTRUST_SERVICES_ATTESTATION_COSE_H
#define WOLFTRUST_SERVICES_ATTESTATION_COSE_H

#include <stddef.h>
#include <stdint.h>

#define WT_ATTEST_COSE_OK                 0
#define WT_ATTEST_COSE_E_BADARG       (-3200)
#define WT_ATTEST_COSE_E_BUFFER       (-3201)
#define WT_ATTEST_COSE_E_SIGN         (-3202)
#define WT_ATTEST_COSE_E_ENCODE       (-3203)

#define WT_ATTEST_COSE_FLAG_UNTAGGED 0x0001u

/*
 * Sign an ES256 digest without exposing the private key. The callback must
 * return a 64-byte raw ECDSA signature containing r followed by s. A return
 * value of zero indicates success.
 */
typedef int (*wt_attest_cose_sign_cb)(void* context,
    const uint8_t* digest, size_t digestSize, uint8_t* signature,
    size_t signatureSize, size_t* signatureLength);

typedef struct wt_attest_cose_signer {
    wt_attest_cose_sign_cb sign;
    void* context;
    const uint8_t* keyId;
    size_t keyIdSize;
} wt_attest_cose_signer_t;

/*
 * Return the exact encoded COSE_Sign1 size without invoking signer->sign.
 * The payload size describes an already encoded EAT claims set.
 */
int wt_attest_cose_sign1_size(const wt_attest_cose_signer_t* signer,
    size_t payloadSize, uint32_t flags, size_t* tokenSize);

/*
 * Wrap an already encoded EAT claims set in COSE_Sign1 and sign it through
 * signer->sign. The caller owns the scratch and token buffers. wolfCOSE
 * clears scratch before returning. Output is tagged unless UNTAGGED is set.
 */
int wt_attest_cose_sign1_encode(const wt_attest_cose_signer_t* signer,
    const uint8_t* payload, size_t payloadSize, uint32_t flags,
    uint8_t* scratch, size_t scratchSize, uint8_t* token,
    size_t tokenCapacity, size_t* tokenSize);

#endif /* WOLFTRUST_SERVICES_ATTESTATION_COSE_H */
