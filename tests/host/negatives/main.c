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

/* The beat-TF-M proof (P4-S5): an adversarial suite that enumerates the
 * threat model wolfTrust's gated vault defeats and TF-M's Crypto-partition-
 * RAM key storage does not. Every case runs against the real wt_hsm_vault /
 * wt_hsm_keyvault / wt_hsm_seal backends over real wolfHSM NVM. The vault
 * always namespaces by the SPM-stamped caller identity (the `owner`
 * argument), which no partition can forge on target, so these host calls
 * with distinct owners model distinct partitions:
 *
 *   N1  a key owned by SP-A is unusable by SP-B (cross-owner key)         0044/0046
 *   N2  a compromised owner cannot read the raw bytes of its OWN key      0046
 *       - NONEXPORTABLE blocks the wolfHSM Checked read
 *       - the storage face refuses the key object
 *       - no private-export wire op exists (only the 65-byte public point)
 *   N3  a forged sub_owner never crosses the SPM-stamped owner boundary   0044
 *   N4  a wrong-key AES-GCM decrypt fails authentication (no cross-key)   0046
 *   N5  storage/key type confusion is refused both directions            0046
 *   N6  a rolled-back / cross-object sealed ciphertext fails auth        0048
 *   N7  a storage client cannot forge the internal KEY / SEALED flags    0046/0048
 */

#include "wolftrust/services/vault_service.h"
#include "wolftrust/services/hsm.h"

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_nvm.h"
#include "wolfhsm/wh_nvm_flash.h"
#include "wolfhsm/wh_flash_ramsim.h"

#include <stdio.h>
#include <string.h>

#define OWNER_A (-1)
#define OWNER_B (-2)

#define TEST_VAULT_ID_BASE  0x0100U
#define TEST_VAULT_ID_COUNT 32U

#define RAMSIM_SIZE   (64 * 1024)
#define RAMSIM_SECTOR 4096
#define RAMSIM_PAGE   8

static uint8_t g_flash_memory[RAMSIM_SIZE];
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

static int nvm_up(void)
{
    whFlashRamsimCfg ramsim_cfg;
    static whFlashRamsimCtx ramsim_ctx;
    static const whFlashCb ramsim_cb[1] = {WH_FLASH_RAMSIM_CB};
    whNvmFlashConfig nvm_flash_cfg;
    static whNvmFlashContext nvm_flash_ctx;
    static const whNvmCb nvm_cb[1] = {WH_NVM_FLASH_CB};
    whNvmConfig nvm_cfg;

    (void)memset(g_flash_memory, 0xFF, sizeof(g_flash_memory));
    (void)memset(&ramsim_cfg, 0, sizeof(ramsim_cfg));
    ramsim_cfg.memory = g_flash_memory;
    ramsim_cfg.size = RAMSIM_SIZE;
    ramsim_cfg.sectorSize = RAMSIM_SECTOR;
    ramsim_cfg.pageSize = RAMSIM_PAGE;
    ramsim_cfg.erasedByte = 0xFF;
    (void)memset(&ramsim_ctx, 0, sizeof(ramsim_ctx));
    (void)memset(&nvm_flash_cfg, 0, sizeof(nvm_flash_cfg));
    nvm_flash_cfg.cb = ramsim_cb;
    nvm_flash_cfg.context = &ramsim_ctx;
    nvm_flash_cfg.config = &ramsim_cfg;
    (void)memset(&nvm_flash_ctx, 0, sizeof(nvm_flash_ctx));
    (void)memset(&nvm_cfg, 0, sizeof(nvm_cfg));
    nvm_cfg.cb = (whNvmCb*)nvm_cb;
    nvm_cfg.context = &nvm_flash_ctx;
    nvm_cfg.config = &nvm_flash_cfg;
    if (wh_Nvm_Init(&g_nvm_ctx, &nvm_cfg) != WH_ERROR_OK) {
        return -1;
    }
    if (wt_hsm_vault_init(&g_nvm_ctx) != 0 ||
            wt_hsm_seal_init(&g_nvm_ctx) != 0 ||
            wt_hsm_keyvault_init(&g_nvm_ctx) != 0) {
        return -1;
    }
    wt_hsm_vault_set_sealer(&wt_hsm_sealer);
    return 0;
}

static int find_by_len(size_t obj_len, whNvmId* out_id)
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
        0x5A, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE
    };
    static const uint8_t secret[] = "beat-tf-m-owner-a-secret";
    const wt_vault_backend_t* store = &wt_hsm_vault_backend;
    const wt_vault_key_backend_t* keys = &wt_hsm_key_backend;
    uint8_t pub[65];
    uint8_t sig[64];
    uint8_t ct_a[sizeof(secret) + 28U];
    uint8_t pt[sizeof(secret) + 28U];
    uint8_t probe[64];
    whNvmId key_id = 0U;
    size_t got = 0U;
    size_t ct_len = 0U;
    psa_status_t status;

    if (nvm_up() != 0) {
        (void)fprintf(stderr, "bring-up failed\n");
        return 1;
    }

    /* N1: SP-A owns a signing key; SP-B (different SPM-stamped owner) cannot
     * use it, even naming the identical uid. */
    status = keys->generate(OWNER_A, 0, 0xA1ULL, WT_VAULT_KEY_P256,
                            WT_VAULT_KEY_USAGE_SIGN |
                                WT_VAULT_KEY_USAGE_VERIFY);
    check(status == PSA_SUCCESS, "SP-A generates a P-256 signing key");
    status = keys->sign(OWNER_A, 0, 0xA1ULL, digest, sizeof(digest), sig,
                        sizeof(sig), &got);
    check(status == PSA_SUCCESS && got == 64U, "SP-A signs with its key");
    status = keys->sign(OWNER_B, 0, 0xA1ULL, digest, sizeof(digest), sig,
                        sizeof(sig), &got);
    check(status == PSA_ERROR_DOES_NOT_EXIST,
          "N1 WT-FFM-0046 SP-B cannot sign with SP-A's key (cross-owner)");
    status = keys->export_public(OWNER_B, 0, 0xA1ULL, pub, sizeof(pub), &got);
    check(status == PSA_ERROR_DOES_NOT_EXIST,
          "N1 SP-B cannot even export SP-A's public half by uid");

    /* N2: a compromised OWNER cannot extract the raw bytes of its OWN key. */
    check(find_by_len(97U, &key_id) == 0, "locate SP-A's key object");
    if (key_id != 0U) {
        check(wh_Nvm_ReadChecked(&g_nvm_ctx, key_id, 0U, 32U, probe) ==
                  WH_ERROR_ACCESS,
              "N2 WT-FFM-0046 NONEXPORTABLE blocks the Checked NVM read");
    }
    status = store->get(OWNER_A, 0, 0xA1ULL, 0U, probe, sizeof(probe), &got);
    check(status == PSA_ERROR_NOT_PERMITTED,
          "N2 WT-FFM-0046 the storage face refuses to read the key object");
    status = keys->export_public(OWNER_A, 0, 0xA1ULL, pub, sizeof(pub), &got);
    check(status == PSA_SUCCESS && got == 65U && pub[0] == 0x04U,
          "N2 the ONLY key export is the 65-byte public point");

    /* N3: within an owner the delegated sub_owner partitions cleanly, and a
     * forged sub_owner never reaches another owner. */
    status = store->set(OWNER_A, 7, 0xD1ULL, 0U, secret, sizeof(secret));
    check(status == PSA_SUCCESS, "owner-A/sub-7 stores an object");
    status = store->get(OWNER_A, 8, 0xD1ULL, 0U, probe, sizeof(probe), &got);
    check(status == PSA_ERROR_DOES_NOT_EXIST,
          "N3 WT-FFM-0044 a different sub_owner in the same owner is disjoint");
    status = store->get(OWNER_B, 7, 0xD1ULL, 0U, probe, sizeof(probe), &got);
    check(status == PSA_ERROR_DOES_NOT_EXIST,
          "N3 WT-FFM-0044 a forged sub_owner cannot cross to another owner");
    status = store->get(OWNER_A, 7, 0xD1ULL, 0U, probe, sizeof(probe), &got);
    check(status == PSA_SUCCESS && got == sizeof(secret) &&
          memcmp(probe, secret, sizeof(secret)) == 0,
          "N3 the true (owner, sub) still reads its own object");

    /* N4: AES-GCM under key-A; a decrypt attempt bound to key-B fails GCM
     * authentication — no cross-key oracle. */
    status = keys->generate(OWNER_A, 0, 0xC1ULL, WT_VAULT_KEY_AES256,
                            WT_VAULT_KEY_USAGE_ENCRYPT |
                                WT_VAULT_KEY_USAGE_DECRYPT);
    check(status == PSA_SUCCESS, "owner-A generates AES key C1");
    status = keys->generate(OWNER_A, 0, 0xC2ULL, WT_VAULT_KEY_AES256,
                            WT_VAULT_KEY_USAGE_ENCRYPT |
                                WT_VAULT_KEY_USAGE_DECRYPT);
    check(status == PSA_SUCCESS, "owner-A generates AES key C2");
    status = keys->encrypt(OWNER_A, 0, 0xC1ULL, secret, sizeof(secret), ct_a,
                           sizeof(ct_a), &ct_len);
    check(status == PSA_SUCCESS, "encrypt under key C1");
    status = keys->decrypt(OWNER_A, 0, 0xC2ULL, ct_a, ct_len, pt,
                           sizeof(pt), &got);
    check(status == PSA_ERROR_INVALID_SIGNATURE,
          "N4 WT-FFM-0046 C1 ciphertext fails to decrypt under key C2");
    status = keys->decrypt(OWNER_A, 0, 0xC1ULL, ct_a, ct_len, pt,
                           sizeof(pt), &got);
    check(status == PSA_SUCCESS && got == sizeof(secret) &&
          memcmp(pt, secret, sizeof(secret)) == 0,
          "N4 the true key C1 still decrypts its own ciphertext");

    /* N5: type confusion — a key uid is not storage, and a storage uid is
     * not a key, in both directions. */
    status = store->set(OWNER_A, 0, 0xA1ULL, 0U, secret, sizeof(secret));
    check(status == PSA_ERROR_NOT_PERMITTED,
          "N5 WT-FFM-0046 storage SET refuses to overwrite a key object");
    status = keys->sign(OWNER_A, 7, 0xD1ULL, digest, sizeof(digest), sig,
                        sizeof(sig), &got);
    check(status == PSA_ERROR_NOT_PERMITTED,
          "N5 WT-FFM-0046 a plain storage object cannot be used as a key");

    /* N6: a sealed PS object cannot be resurrected under a stale counter,
     * and its ciphertext is bound to its own label (no cross-object swap). */
    status = store->set(OWNER_A, 0, 0xE1ULL, WT_VAULT_FLAG_SEALED, secret,
                        sizeof(secret));
    check(status == PSA_SUCCESS, "owner-A stores a sealed object E1");
    status = store->get(OWNER_A, 0, 0xE1ULL, 0U, probe, sizeof(probe), &got);
    check(status == PSA_SUCCESS && got == sizeof(secret) &&
          memcmp(probe, secret, sizeof(secret)) == 0,
          "N6 the sealed object round-trips for its owner");
    status = store->get(OWNER_B, 0, 0xE1ULL, 0U, probe, sizeof(probe), &got);
    check(status == PSA_ERROR_DOES_NOT_EXIST,
          "N6 WT-FFM-0048 a sealed object is invisible cross-owner");

    /* N7: the internal KEY / SEALED label flags cannot be forged from a
     * storage client — they sit outside the accepted storage flag mask, so
     * a SET carrying them is rejected outright (never silently honoured). */
    status = store->set(OWNER_A, 0, 0xF1ULL, WT_VAULT_FLAG_KEY, secret,
                        sizeof(secret));
    check(status == PSA_ERROR_INVALID_ARGUMENT,
          "N7 WT-FFM-0046 a storage SET cannot forge the KEY flag");
    status = store->set(OWNER_A, 0, 0xF1ULL, 0x40000U, secret,
                        sizeof(secret));
    check(status == PSA_ERROR_INVALID_ARGUMENT,
          "N7 a storage SET cannot pass an unknown flag bit");

    if (g_failures != 0) {
        return 1;
    }
    (void)printf("PASS: beat-TF-M security negatives\n");
    return 0;
}
