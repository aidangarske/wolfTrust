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

/* Host proof of the vault key-ops (P4-S4, WT-FFM-0046): the full chain — a
 * Non-secure client calling SERVICE_CRYPTO key ops, the crypto dispatch
 * forwarding over SP-to-SP FF-M IPC to SERVICE_VAULT, the wt_hsm_keyvault
 * backend running real wolfCrypt ECC P-256 / AES-256-GCM inside the
 * privileged domain, keys stored NONEXPORTABLE in real wolfHSM NVM on the
 * RAM flash simulator. Negative evidence: private material unreachable via
 * the Checked NVM path AND via the storage face; usage policy enforced;
 * cross-client keys invisible; keys and their public halves survive a
 * simulated reboot. */

#include "wolftrust/ffm.h"
#include "wolftrust/services/crypto_service.h"
#include "wolftrust/services/vault_service.h"
#include "wolftrust/services/hsm.h"

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_flash_ramsim.h"

#include <stdio.h>
#include <string.h>

#define TEST_VAULT_PARTITION  5
#define TEST_VAULT_SID        4098U
#define TEST_CRYPTO_PARTITION 4
#define TEST_CRYPTO_SID       4097U
#define TEST_NS_GUEST0        (-1)
#define TEST_NS_GUEST1        (-2)

#define TEST_VAULT_ID_BASE  0x0100U
#define TEST_VAULT_ID_COUNT 32U

#define RAMSIM_SIZE   (64 * 1024)
#define RAMSIM_SECTOR 4096
#define RAMSIM_PAGE   8

static uint8_t g_flash_memory[RAMSIM_SIZE];
static uint8_t g_flash_snapshot[RAMSIM_SIZE];

static whFlashRamsimCfg g_ramsim_cfg;
static whFlashRamsimCtx g_ramsim_ctx;
static const whFlashCb g_ramsim_cb[1] = {WH_FLASH_RAMSIM_CB};
static whNvmFlashConfig g_nvm_flash_cfg;
static whNvmFlashContext g_nvm_flash_ctx;
static const whNvmCb g_nvm_cb[1] = {WH_NVM_FLASH_CB};
static whNvmConfig g_nvm_cfg;
static whNvmContext g_nvm_ctx;

static int g_failures;

static void check(int ok, const char* what)
{
    if (ok) {
        (void)printf("PASS: %s\n", what);
    } else {
        (void)printf("FAIL: %s\n", what);
        g_failures++;
    }
}

static int test_check_read(void* context, psa_client_id_t caller,
                           const void* address, size_t size)
{
    (void)context;
    (void)caller;
    return size == 0U || address != NULL;
}

static int test_check_write(void* context, psa_client_id_t caller,
                            void* address, size_t size)
{
    (void)context;
    (void)caller;
    return size == 0U || address != NULL;
}

static void test_panic(void* context, int32_t partition_id)
{
    (void)context;
    (void)partition_id;
}

static int test_dispatch(void* context, wt_ffm_runtime_t* runtime,
                         int32_t partition_id)
{
    (void)context;
    (void)runtime;
    (void)partition_id;
    return WT_FFM_ERROR_STATE;
}

static const wt_ffm_port_ops_t g_port_ops = {
    test_check_read,
    test_check_write,
    test_dispatch,
    test_panic
};

static const wt_service_descriptor_t g_vault_services[] = {
    {
        "SERVICE_VAULT", TEST_VAULT_SID, 1U, WT_SERVICE_VERSION_RELAXED,
        0x10U, 0U, 0U, 1U
    }
};

static const wt_service_descriptor_t g_crypto_services[] = {
    {
        "SERVICE_CRYPTO", TEST_CRYPTO_SID, 1U, WT_SERVICE_VERSION_RELAXED,
        0x10U, 0U, 1U, 1U
    }
};

static const uint32_t g_crypto_deps[] = { TEST_VAULT_SID };

static const wt_partition_manifest_t g_partitions[] = {
    {
        "PARTITION_VAULT", TEST_VAULT_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_vault_services, 1U, NULL, 0U, NULL, 0U
    },
    {
        "PARTITION_CRYPTO", TEST_CRYPTO_PARTITION, WT_FFM_VERSION_1_0,
        WT_PARTITION_MODEL_IPC, WT_PARTITION_PRIORITY_NORMAL,
        g_crypto_services, 1U, g_crypto_deps, 1U, NULL, 0U
    }
};

static const wt_system_manifest_t g_manifest = {
    .format_version = WT_MANIFEST_FORMAT_VERSION,
    .generator_version = "keyvault-test",
    .features = WT_MANIFEST_FEATURE_IPC,
    .partitions = g_partitions,
    .partition_count = sizeof(g_partitions) / sizeof(g_partitions[0])
};

static int test_nvm_up(int reboot)
{
    (void)memset(&g_ramsim_cfg, 0, sizeof(g_ramsim_cfg));
    g_ramsim_cfg.memory = g_flash_memory;
    g_ramsim_cfg.size = RAMSIM_SIZE;
    g_ramsim_cfg.sectorSize = RAMSIM_SECTOR;
    g_ramsim_cfg.pageSize = RAMSIM_PAGE;
    g_ramsim_cfg.erasedByte = 0xFF;
    if (reboot != 0) {
        (void)memcpy(g_flash_snapshot, g_flash_memory, RAMSIM_SIZE);
        g_ramsim_cfg.initData = g_flash_snapshot;
    }
    (void)memset(&g_ramsim_ctx, 0, sizeof(g_ramsim_ctx));
    (void)memset(&g_nvm_flash_cfg, 0, sizeof(g_nvm_flash_cfg));
    g_nvm_flash_cfg.cb = g_ramsim_cb;
    g_nvm_flash_cfg.context = &g_ramsim_ctx;
    g_nvm_flash_cfg.config = &g_ramsim_cfg;
    (void)memset(&g_nvm_flash_ctx, 0, sizeof(g_nvm_flash_ctx));
    (void)memset(&g_nvm_cfg, 0, sizeof(g_nvm_cfg));
    g_nvm_cfg.cb = (whNvmCb*)g_nvm_cb;
    g_nvm_cfg.context = &g_nvm_flash_ctx;
    g_nvm_cfg.config = &g_nvm_flash_cfg;
    (void)memset(&g_nvm_ctx, 0, sizeof(g_nvm_ctx));
    if (wh_Nvm_Init(&g_nvm_ctx, &g_nvm_cfg) != WH_ERROR_OK) {
        return -1;
    }
    if (wt_hsm_vault_init(&g_nvm_ctx) != 0) {
        return -1;
    }
    wt_vault_service_set_backend(&wt_hsm_vault_backend);
    if (wt_hsm_keyvault_init(&g_nvm_ctx) != 0) {
        return -1;
    }
    wt_vault_service_set_key_backend(&wt_hsm_key_backend);
    return 0;
}

static int test_runtime_up(wt_ffm_runtime_t* runtime,
                           wt_crypto_service_ctx_t* crypto_ctx)
{
    if (wt_ffm_init(runtime, &g_manifest, &g_port_ops, NULL) !=
            WT_FFM_SUCCESS) {
        return -1;
    }
    if (wt_ffm_register_partition(runtime, TEST_VAULT_PARTITION,
                                  wt_vault_service_dispatch, NULL) !=
            WT_FFM_SUCCESS) {
        return -1;
    }
    (void)memset(crypto_ctx, 0, sizeof(*crypto_ctx));
    crypto_ctx->transport = wt_spm_transport_direct;
    crypto_ctx->compute = wt_crypto_sp_hash;
    crypto_ctx->vault_sid = TEST_VAULT_SID;
    crypto_ctx->vault_handle = 0;
    if (wt_ffm_register_partition(runtime, TEST_CRYPTO_PARTITION,
                                  wt_crypto_service_dispatch, crypto_ctx) !=
            WT_FFM_SUCCESS) {
        return -1;
    }
    return 0;
}

/* Client-face helper: [wt_crypto_key_req_t][payload] in one input vector. */
static psa_status_t key_call(wt_ffm_runtime_t* runtime, int32_t caller,
                             psa_handle_t handle, int32_t op, uint64_t uid,
                             uint32_t usage, uint32_t key_type,
                             const void* payload, size_t payload_len,
                             void* out, size_t out_cap, size_t* out_len)
{
    uint8_t buffer[sizeof(wt_crypto_key_req_t) + 256U];
    wt_crypto_key_req_t req;
    psa_invec in_vec[1];
    psa_outvec out_vec[1];
    psa_status_t status;

    if (payload_len > 256U) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    (void)memset(&req, 0, sizeof(req));
    req.uid = uid;
    req.usage = usage;
    req.key_type = key_type;
    (void)memcpy(buffer, &req, sizeof(req));
    if (payload != NULL) {
        (void)memcpy(buffer + sizeof(req), payload, payload_len);
    }
    in_vec[0].base = buffer;
    in_vec[0].len = sizeof(req) + payload_len;
    out_vec[0].base = out;
    out_vec[0].len = out_cap;
    status = wt_ffm_call(runtime, caller, handle, op, in_vec, 1U,
                         (out != NULL) ? out_vec : NULL,
                         (out != NULL) ? 1U : 0U);
    if (out_len != NULL) {
        *out_len = out_vec[0].len;
    }
    return status;
}

/* Find the NVM id of the single object whose stored length matches. */
static int test_find_stored(size_t obj_len, whNvmId* out_id)
{
    whNvmMetadata meta;
    whNvmId id;
    uint32_t i;

    for (i = 0U; i < TEST_VAULT_ID_COUNT; i++) {
        id = (whNvmId)(TEST_VAULT_ID_BASE + i);
        if (wh_Nvm_GetMetadata(&g_nvm_ctx, id, &meta) == WH_ERROR_OK &&
                meta.len == obj_len) {
            *out_id = id;
            return 0;
        }
    }
    return -1;
}

int main(void)
{
    static const uint8_t digest[32] = {
        0x57, 0x54, 0x4B, 0x56, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
        0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
        0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C
    };
    static const uint8_t import_d[32] = {
        0x2A, 0xF1, 0x40, 0x7E, 0x33, 0x91, 0x5C, 0x08,
        0x66, 0x12, 0xE9, 0x4B, 0x7D, 0xC3, 0x55, 0x19,
        0xB0, 0x4A, 0x8F, 0x27, 0xD1, 0x63, 0x0E, 0x72,
        0x98, 0x3C, 0x5F, 0xA4, 0x11, 0xE8, 0x36, 0x6D
    };
    static const uint8_t plaintext[] = "keyvault-aes-gcm-plaintext";
    uint8_t pub_k1[65];
    uint8_t pub_k4[65];
    uint8_t sig[64];
    uint8_t verify_buf[32 + 64];
    uint8_t ct[sizeof(plaintext) + 28U];
    uint8_t pt[sizeof(plaintext)];
    uint8_t probe[32];
    whNvmId key_id = 0U;
    wt_ffm_runtime_t runtime;
    wt_crypto_service_ctx_t crypto_ctx;
    psa_handle_t handle_g0;
    psa_handle_t handle_g1;
    size_t got = 0U;
    size_t ct_len = 0U;
    psa_status_t status;

    (void)memset(g_flash_memory, 0xFF, sizeof(g_flash_memory));
    if (test_nvm_up(0) != 0) {
        (void)fprintf(stderr, "NVM/keyvault bring-up failed\n");
        return 1;
    }
    if (test_runtime_up(&runtime, &crypto_ctx) != 0) {
        (void)fprintf(stderr, "runtime bring-up failed\n");
        return 1;
    }

    handle_g0 = wt_ffm_connect(&runtime, TEST_NS_GUEST0, TEST_CRYPTO_SID, 1U);
    check(PSA_HANDLE_IS_VALID(handle_g0),
          "NS guest0 connects to SERVICE_CRYPTO");
    handle_g1 = wt_ffm_connect(&runtime, TEST_NS_GUEST1, TEST_CRYPTO_SID, 1U);
    check(PSA_HANDLE_IS_VALID(handle_g1),
          "NS guest1 connects to SERVICE_CRYPTO");
    if (!PSA_HANDLE_IS_VALID(handle_g0) || !PSA_HANDLE_IS_VALID(handle_g1)) {
        return 1;
    }

    /* The chain: NS -> crypto SP -> gate -> vault -> wolfCrypt -> NVM. */
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_GENERATE, 0x4B01ULL,
                      WT_VAULT_KEY_USAGE_SIGN | WT_VAULT_KEY_USAGE_VERIFY,
                      WT_VAULT_KEY_P256, NULL, 0U, NULL, 0U, NULL);
    check(status == PSA_SUCCESS,
          "WT-FFM-0046 guest0 generates P-256 key in the vault");
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_GENERATE, 0x4B01ULL,
                      WT_VAULT_KEY_USAGE_SIGN, WT_VAULT_KEY_P256, NULL, 0U,
                      NULL, 0U, NULL);
    check(status == PSA_ERROR_ALREADY_EXISTS,
          "second generate on the same uid refused ALREADY_EXISTS");

    (void)memset(pub_k1, 0, sizeof(pub_k1));
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_EXPORT_PUBLIC, 0x4B01ULL, 0U, 0U,
                      NULL, 0U, pub_k1, sizeof(pub_k1), &got);
    check(status == PSA_SUCCESS && got == 65U && pub_k1[0] == 0x04U,
          "export_public returns the X9.63 public point");

    (void)memset(sig, 0, sizeof(sig));
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_SIGN, 0x4B01ULL, 0U, 0U,
                      digest, sizeof(digest), sig, sizeof(sig), &got);
    check(status == PSA_SUCCESS && got == 64U,
          "WT-FFM-0046 sign inside the vault returns raw r||s");

    (void)memcpy(verify_buf, digest, 32U);
    (void)memcpy(verify_buf + 32U, sig, 64U);
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_VERIFY, 0x4B01ULL, 0U, 0U,
                      verify_buf, sizeof(verify_buf), NULL, 0U, NULL);
    check(status == PSA_SUCCESS, "verify accepts the vault signature");

    verify_buf[0] ^= 0x01U;
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_VERIFY, 0x4B01ULL, 0U, 0U,
                      verify_buf, sizeof(verify_buf), NULL, 0U, NULL);
    check(status == PSA_ERROR_INVALID_SIGNATURE,
          "verify rejects a tampered digest");
    verify_buf[0] ^= 0x01U;
    verify_buf[40] ^= 0x01U;
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_VERIFY, 0x4B01ULL, 0U, 0U,
                      verify_buf, sizeof(verify_buf), NULL, 0U, NULL);
    check(status == PSA_ERROR_INVALID_SIGNATURE,
          "verify rejects a tampered signature");

    /* Usage policy enforced at the vault, not by caller convention. */
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_GENERATE, 0x4B02ULL,
                      WT_VAULT_KEY_USAGE_VERIFY, WT_VAULT_KEY_P256, NULL, 0U,
                      NULL, 0U, NULL);
    check(status == PSA_SUCCESS, "guest0 generates verify-only key");
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_SIGN, 0x4B02ULL, 0U, 0U,
                      digest, sizeof(digest), sig, sizeof(sig), &got);
    check(status == PSA_ERROR_NOT_PERMITTED,
          "WT-FFM-0046 sign with a verify-only key refused");

    /* Import path: derive-and-store, then a full sign/verify round trip. */
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_IMPORT, 0x4B03ULL,
                      WT_VAULT_KEY_USAGE_SIGN | WT_VAULT_KEY_USAGE_VERIFY,
                      WT_VAULT_KEY_P256, import_d, sizeof(import_d),
                      NULL, 0U, NULL);
    check(status == PSA_SUCCESS, "guest0 imports a P-256 private scalar");
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_SIGN, 0x4B03ULL, 0U, 0U,
                      digest, sizeof(digest), sig, sizeof(sig), &got);
    (void)memcpy(verify_buf, digest, 32U);
    (void)memcpy(verify_buf + 32U, sig, 64U);
    check(status == PSA_SUCCESS &&
          key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                   WT_CRYPTO_OP_KEY_VERIFY, 0x4B03ULL, 0U, 0U, verify_buf,
                   sizeof(verify_buf), NULL, 0U, NULL) == PSA_SUCCESS,
          "imported key signs and verifies");

    /* WT-FFM-0046 non-exportability, at every layer that could leak:
     * the Checked NVM read refuses the key object, and the storage face
     * refuses key-flagged objects even for the owning namespace. */
    check(test_find_stored(97U, &key_id) == 0, "locate stored P-256 object");
    if (key_id != 0U) {
        check(wh_Nvm_ReadChecked(&g_nvm_ctx, key_id, 0U, 32U, probe) ==
                  WH_ERROR_ACCESS,
              "WT-FFM-0046 NONEXPORTABLE blocks the Checked NVM read");
    }
    status = wt_hsm_vault_backend.get(TEST_CRYPTO_PARTITION, TEST_NS_GUEST0,
                                      0x4B01ULL, 0U, probe, sizeof(probe),
                                      &got);
    check(status == PSA_ERROR_NOT_PERMITTED,
          "WT-FFM-0046 storage face refuses reading a key object");
    status = wt_hsm_vault_backend.set(TEST_CRYPTO_PARTITION, TEST_NS_GUEST0,
                                      0x4B01ULL, 0U, digest, sizeof(digest));
    check(status == PSA_ERROR_NOT_PERMITTED,
          "WT-FFM-0046 storage face refuses overwriting a key object");

    /* WT-FFM-0044: same uid, different end client — invisible. */
    status = key_call(&runtime, TEST_NS_GUEST1, handle_g1,
                      WT_CRYPTO_OP_KEY_SIGN, 0x4B01ULL, 0U, 0U,
                      digest, sizeof(digest), sig, sizeof(sig), &got);
    check(status == PSA_ERROR_DOES_NOT_EXIST,
          "WT-FFM-0044 guest1 cannot use guest0's key");

    /* AES-256-GCM under a stored key. */
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_GENERATE, 0x4B04ULL,
                      WT_VAULT_KEY_USAGE_ENCRYPT | WT_VAULT_KEY_USAGE_DECRYPT,
                      WT_VAULT_KEY_AES256, NULL, 0U, NULL, 0U, NULL);
    check(status == PSA_SUCCESS, "guest0 generates AES-256 key in the vault");
    (void)memset(ct, 0, sizeof(ct));
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_ENCRYPT, 0x4B04ULL, 0U, 0U,
                      plaintext, sizeof(plaintext), ct, sizeof(ct), &ct_len);
    check(status == PSA_SUCCESS && ct_len == sizeof(plaintext) + 28U &&
          memcmp(ct + 12U, plaintext, sizeof(plaintext)) != 0,
          "encrypt returns [nonce][ct][tag] with no plaintext leak");
    (void)memset(pt, 0, sizeof(pt));
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_DECRYPT, 0x4B04ULL, 0U, 0U,
                      ct, ct_len, pt, sizeof(pt), &got);
    check(status == PSA_SUCCESS && got == sizeof(plaintext) &&
          memcmp(pt, plaintext, sizeof(plaintext)) == 0,
          "decrypt round-trips the plaintext");
    ct[14] ^= 0x01U;
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_DECRYPT, 0x4B04ULL, 0U, 0U,
                      ct, ct_len, pt, sizeof(pt), &got);
    check(status == PSA_ERROR_INVALID_SIGNATURE,
          "decrypt rejects tampered ciphertext");
    ct[14] ^= 0x01U;

    /* Destroy lifecycle. */
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_DESTROY, 0x4B02ULL, 0U, 0U,
                      NULL, 0U, NULL, 0U, NULL);
    check(status == PSA_SUCCESS, "destroy removes the verify-only key");
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_VERIFY, 0x4B02ULL, 0U, 0U,
                      verify_buf, sizeof(verify_buf), NULL, 0U, NULL);
    check(status == PSA_ERROR_DOES_NOT_EXIST, "destroyed key is gone");

    /* Reboot persistence: keys and public halves must be stable. */
    if (wt_ffm_close(&runtime, TEST_NS_GUEST0, handle_g0) != WT_FFM_SUCCESS ||
            wt_ffm_close(&runtime, TEST_NS_GUEST1, handle_g1) !=
                WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "psa_close(SERVICE_CRYPTO) failed\n");
        return 1;
    }
    if (test_nvm_up(1) != 0 || test_runtime_up(&runtime, &crypto_ctx) != 0) {
        (void)fprintf(stderr, "reboot bring-up failed\n");
        return 1;
    }
    handle_g0 = wt_ffm_connect(&runtime, TEST_NS_GUEST0, TEST_CRYPTO_SID, 1U);
    check(PSA_HANDLE_IS_VALID(handle_g0),
          "guest0 reconnects to SERVICE_CRYPTO after reboot");
    (void)memset(pub_k4, 0, sizeof(pub_k4));
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_EXPORT_PUBLIC, 0x4B01ULL, 0U, 0U,
                      NULL, 0U, pub_k4, sizeof(pub_k4), &got);
    check(status == PSA_SUCCESS &&
          memcmp(pub_k4, pub_k1, sizeof(pub_k1)) == 0,
          "public point unchanged across reboot");
    (void)memset(sig, 0, sizeof(sig));
    status = key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                      WT_CRYPTO_OP_KEY_SIGN, 0x4B01ULL, 0U, 0U,
                      digest, sizeof(digest), sig, sizeof(sig), &got);
    (void)memcpy(verify_buf, digest, 32U);
    (void)memcpy(verify_buf + 32U, sig, 64U);
    check(status == PSA_SUCCESS &&
          key_call(&runtime, TEST_NS_GUEST0, handle_g0,
                   WT_CRYPTO_OP_KEY_VERIFY, 0x4B01ULL, 0U, 0U, verify_buf,
                   sizeof(verify_buf), NULL, 0U, NULL) == PSA_SUCCESS,
          "WT-FFM-0046 key signs and verifies after reboot");

    if (wt_ffm_close(&runtime, TEST_NS_GUEST0, handle_g0) != WT_FFM_SUCCESS) {
        (void)fprintf(stderr, "psa_close after reboot failed\n");
        return 1;
    }

    if (g_failures != 0) {
        return 1;
    }
    (void)printf("PASS: vault key-ops chain through SERVICE_CRYPTO\n");
    return 0;
}
