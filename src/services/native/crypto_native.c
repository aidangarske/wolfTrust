/* crypto_native.c
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
 * Native crypto engine (WT_ENGINE=native): direct wolfCrypt dispatch behind
 * the unchanged SERVICE_HSM door. wt_native_init is the boot bring-up peer
 * of wt_hsm_init; wt_native_submit services the native wire at the relay
 * seam; the attestation signer runs wc_ecc against a vault-stored IAK.
 */

/* wolfCrypt settings must come first. */
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/types.h"
#include "wolfssl/wolfcrypt/sha256.h"
#include "wolfssl/wolfcrypt/error-crypt.h"

#include "wolfhsm/wh_error.h"

#include "wolftrust/types.h"
#include "wolftrust/sync/mutex.h"
#include "wolftrust/nvm_store.h"
#include "wolftrust/services/hsm.h"
#include "wolftrust/services/hsm_relay.h"
#include "wolftrust/services/vault_service.h"
#include "wolftrust/services/crypto_native.h"

#include "wolftrust/port_nvm.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* The IAK vault home: owner/sub 0 is reachable by no SPM-stamped caller
 * (partitions are positive, NS clients negative), so only the boot and
 * attestation paths below can address it. */
#define WT_NATIVE_IAK_OWNER 0
#define WT_NATIVE_IAK_SUB   0
#define WT_NATIVE_IAK_UID   0xF0u

static bool g_native_attest_ready;

int wt_native_init(void)
{
    int rc;

    rc = wolfCrypt_Init();
    if (rc != 0) {
        return rc;
    }

    rc = g_wt_hsm_flash_cb.Init(wt_hsm_flash_context(),
                                wt_hsm_flash_config());
    if (rc != 0) {
        return rc;
    }

    /* Initialise the shared NVM lock once, before wh_Nvm_Init wires it in. */
    wt_mutex_init(&g_wt_nvm_lock_mutex);

    rc = wt_nvm_store_bind();
    if (rc != WH_ERROR_OK) {
        return rc;
    }

    if (wt_hsm_vault_init(&g_wt_nvm_ctx) == 0) {
        wt_vault_service_set_backend(&wt_hsm_vault_backend);
        if (wt_hsm_seal_init(&g_wt_nvm_ctx) == 0) {
            wt_hsm_vault_set_sealer(&wt_hsm_sealer);
        }
        else {
            wt_hsm_vault_set_sealer(NULL);
        }
        if (wt_hsm_keyvault_init(&g_wt_nvm_ctx) == 0) {
            wt_vault_service_set_key_backend(&wt_hsm_key_backend);
        }
        wt_vault_service_set_rng(wt_hsm_keyvault_random);
    }

    return 0;
}

/* =========================================================================
 * Attestation (native): the IAK is a vault key object, provisioned at boot
 * and exercised through the key backend so private material never leaves
 * the privileged vault domain. Same wire forms as the hsm engine: 64-byte
 * r||s signatures, 65-byte X9.63 public point.
 * ====================================================================== */
int wt_hsm_attest_init(void)
{
    uint8_t publicKey[WT_VAULT_KEY_PUB_LEN];
    size_t publicKeySize = 0U;
    psa_status_t status;

    if (g_native_attest_ready) {
        return WH_ERROR_OK;
    }
    status = wt_hsm_key_backend.export_public(WT_NATIVE_IAK_OWNER,
                                              WT_NATIVE_IAK_SUB,
                                              WT_NATIVE_IAK_UID, publicKey,
                                              sizeof(publicKey),
                                              &publicKeySize);
    if (status == PSA_ERROR_DOES_NOT_EXIST) {
        status = wt_hsm_key_backend.generate(WT_NATIVE_IAK_OWNER,
                                             WT_NATIVE_IAK_SUB,
                                             WT_NATIVE_IAK_UID,
                                             WT_VAULT_KEY_P256,
                                             WT_VAULT_KEY_USAGE_SIGN |
                                                 WT_VAULT_KEY_USAGE_VERIFY);
        if (status == PSA_SUCCESS) {
            status = wt_hsm_key_backend.export_public(WT_NATIVE_IAK_OWNER,
                                                      WT_NATIVE_IAK_SUB,
                                                      WT_NATIVE_IAK_UID,
                                                      publicKey,
                                                      sizeof(publicKey),
                                                      &publicKeySize);
        }
        else if (status != PSA_SUCCESS && wt_nvm_reformat_allowed()) {
            /* A foreign or corrupt pool can hold the IAK slot; in an
             * unlocked lifecycle reformat once and re-provision, mirroring
             * the hsm engine's recovery. A SECURED device fails closed. */
            if (wt_hsm_flash_format() == 0 && wt_native_init() == 0) {
                wt_nvm_mark_reformatted();
                status = wt_hsm_key_backend.generate(WT_NATIVE_IAK_OWNER,
                                                     WT_NATIVE_IAK_SUB,
                                                     WT_NATIVE_IAK_UID,
                                                     WT_VAULT_KEY_P256,
                                                     WT_VAULT_KEY_USAGE_SIGN |
                                                     WT_VAULT_KEY_USAGE_VERIFY);
            }
        }
    }
    if (status != PSA_SUCCESS) {
        return WH_ERROR_ABORTED;
    }
    g_native_attest_ready = true;
    return WH_ERROR_OK;
}

int wt_hsm_attest_bootstrap(void)
{
    /* No server tasklet to pump in the native engine: provisioning runs on
     * the boot stack against the vault directly. */
    return wt_hsm_attest_init();
}

int wt_hsm_attest_sign(const uint8_t* digest, size_t digestSize,
                       uint8_t* signature, size_t signatureCapacity,
                       size_t* signatureSize)
{
    psa_status_t status;

    if (!g_native_attest_ready) {
        return WH_ERROR_NOTREADY;
    }
    status = wt_hsm_key_backend.sign(WT_NATIVE_IAK_OWNER, WT_NATIVE_IAK_SUB,
                                     WT_NATIVE_IAK_UID, digest, digestSize,
                                     signature, signatureCapacity,
                                     signatureSize);
    return (status == PSA_SUCCESS) ? WH_ERROR_OK : WH_ERROR_ABORTED;
}

int wt_hsm_attest_public_key(uint8_t* publicKey, size_t publicKeyCapacity,
                             size_t* publicKeySize)
{
    psa_status_t status;

    if (!g_native_attest_ready) {
        return WH_ERROR_NOTREADY;
    }
    status = wt_hsm_key_backend.export_public(WT_NATIVE_IAK_OWNER,
                                              WT_NATIVE_IAK_SUB,
                                              WT_NATIVE_IAK_UID, publicKey,
                                              publicKeyCapacity,
                                              publicKeySize);
    return (status == PSA_SUCCESS) ? WH_ERROR_OK : WH_ERROR_ABORTED;
}

/* =========================================================================
 * wt_native_submit — SERVICE_HSM's native wire backend.
 *
 * One request packet ([wt_crypto_wire_req_t][payload]) in, one response
 * packet ([int32_t psa_status][payload]) out, both bounded by the relay's
 * copied buffers. Key ops execute in the key backend with the SPM-stamped
 * client as delegated sub_owner, so a client only reaches its own keys.
 * ====================================================================== */
static psa_status_t wt_native_hash(const uint8_t* input, size_t input_len,
                                   uint8_t* out, size_t out_cap,
                                   size_t* out_len)
{
    wc_Sha256 sha;
    int rc;

    if (out_cap < WC_SHA256_DIGEST_SIZE) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }
    rc = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (rc == 0) {
        rc = wc_Sha256Update(&sha, input, (word32)input_len);
    }
    if (rc == 0) {
        rc = wc_Sha256Final(&sha, out);
    }
    if (rc != 0) {
        return PSA_ERROR_GENERIC_ERROR;
    }
    *out_len = WC_SHA256_DIGEST_SIZE;
    return PSA_SUCCESS;
}

int wt_native_submit(void* submit_ctx, int32_t client_id, const uint8_t* req,
                     size_t req_len, uint8_t* resp, size_t resp_cap,
                     size_t* resp_len)
{
    wt_crypto_wire_req_t hdr;
    int32_t owner = (int32_t)(intptr_t)submit_ctx;
    const uint8_t* payload;
    uint8_t* out;
    size_t payload_len;
    size_t out_cap;
    size_t out_len = 0U;
    size_t rand_len;
    int32_t wire_status;
    psa_status_t status;

    if (req == NULL || resp == NULL || resp_len == NULL ||
            req_len < sizeof(hdr) || resp_cap < sizeof(wire_status)) {
        return -1;
    }
    (void)memcpy(&hdr, req, sizeof(hdr));
    payload = req + sizeof(hdr);
    payload_len = req_len - sizeof(hdr);
    out = resp + sizeof(wire_status);
    out_cap = resp_cap - sizeof(wire_status);

    switch (hdr.op) {
    case WT_CRYPTO_OP_KEY_GENERATE:
        status = wt_hsm_key_backend.generate(owner, client_id, hdr.uid,
                                             hdr.key_type, hdr.usage);
        break;
    case WT_CRYPTO_OP_KEY_IMPORT:
        status = wt_hsm_key_backend.import(owner, client_id, hdr.uid,
                                           hdr.key_type, hdr.usage, payload,
                                           payload_len);
        break;
    case WT_CRYPTO_OP_KEY_EXPORT_PUBLIC:
        status = wt_hsm_key_backend.export_public(owner, client_id, hdr.uid,
                                                  out, out_cap, &out_len);
        break;
    case WT_CRYPTO_OP_KEY_SIGN:
        status = wt_hsm_key_backend.sign(owner, client_id, hdr.uid, payload,
                                         payload_len, out, out_cap,
                                         &out_len);
        break;
    case WT_CRYPTO_OP_KEY_VERIFY:
        /* payload = [digest 32][signature 64]. */
        if (payload_len != WT_VAULT_KEY_DIGEST_LEN + WT_VAULT_KEY_SIG_LEN) {
            status = PSA_ERROR_INVALID_ARGUMENT;
        }
        else {
            status = wt_hsm_key_backend.verify(owner, client_id, hdr.uid,
                                               payload,
                                               WT_VAULT_KEY_DIGEST_LEN,
                                               payload +
                                                   WT_VAULT_KEY_DIGEST_LEN,
                                               WT_VAULT_KEY_SIG_LEN);
        }
        break;
    case WT_CRYPTO_OP_KEY_ENCRYPT:
        status = wt_hsm_key_backend.encrypt(owner, client_id, hdr.uid,
                                            payload, payload_len, out,
                                            out_cap, &out_len);
        break;
    case WT_CRYPTO_OP_KEY_DECRYPT:
        status = wt_hsm_key_backend.decrypt(owner, client_id, hdr.uid,
                                            payload, payload_len, out,
                                            out_cap, &out_len);
        break;
    case WT_CRYPTO_OP_KEY_DESTROY:
        status = wt_hsm_vault_backend.remove(owner, client_id, hdr.uid);
        break;
    case WT_CRYPTO_OP_RANDOM:
        rand_len = hdr.usage;
        if (rand_len == 0U || rand_len > WT_CRYPTO_RANDOM_MAX ||
                rand_len > out_cap) {
            status = PSA_ERROR_INVALID_ARGUMENT;
        }
        else {
            status = wt_hsm_keyvault_random(out, rand_len);
            if (status == PSA_SUCCESS) {
                out_len = rand_len;
            }
        }
        break;
    case WT_CRYPTO_OP_HASH:
        status = wt_native_hash(payload, payload_len, out, out_cap,
                                &out_len);
        break;
    default:
        status = PSA_ERROR_NOT_SUPPORTED;
        break;
    }

    wire_status = (int32_t)status;
    (void)memcpy(resp, &wire_status, sizeof(wire_status));
    *resp_len = sizeof(wire_status) + out_len;
    return 0;
}
