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

/* guest0_psa is the PSA Crypto and Initial Attestation lifecycle test.
 *
 * Exercises three PSA primitives that, with wolfPSA_SetDefaultDevID()
 * pointed at WH_DEV_ID by the wolfpsa module's SYS_INIT hook, traverse:
 *
 *   psa_*()  →  wolfPSA  →  wolfCrypt(WH_DEV_ID)  →  crypto_cb
 *            →  wh_Client_CryptoCb  →  CMSE Submit/Poll  →  secure wolfHSM
 *
 * Persistent-key + ITS samples need a key-storage backend wolfPSA doesn't
 * yet provide on this port, so this app keeps to volatile-key /
 * stateless ops:
 *   - psa_generate_random()  → wolfHSM RNG
 *   - psa_hash_compute(SHA-256)  → wolfHSM SHA-256
 *   - psa_cipher_encrypt(AES-CTR, volatile key)  → wolfHSM AES
 *
 * The runner's `--uarts` mode asserts on the corresponding LOG lines.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/tee.h>
#include <zephyr/logging/log.h>

#include <psa/crypto.h>
#include <psa/initial_attestation.h>

#include <wolftrust/attestation.h>
#include <wolftrust/zephyr/client.h>

#include "attestation_verify.h"

LOG_MODULE_REGISTER(guest0_psa, LOG_LEVEL_INF);

#define WOLFTRUST_FN_HSM_CANCEL 2u

#ifndef WT_EXPECTED_MEASUREMENT_HEX
#define WT_EXPECTED_MEASUREMENT_HEX ""
#endif

#ifndef WT_EXPECTED_LIFECYCLE
#define WT_EXPECTED_LIFECYCLE 0x3000u
#endif

#define WT_PSA_LIFECYCLE_SECURED 0x3000u

/* Mirror guest0's TEE-driver smoke so the runner's existing TEE assertions
 * stay green and we don't need a second runner mode. */
static void exercise_tee_driver(void)
{
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_version_info ver;
	struct tee_invoke_func_arg arg;
	int rc;

	if (tee == NULL || !device_is_ready(tee)) {
		LOG_WRN("wolftrust TEE device not present/ready");
		return;
	}
	rc = tee_get_version(tee, &ver);
	if (rc != 0) {
		LOG_WRN("tee_get_version rc=%d", rc);
		return;
	}
	LOG_INF("tee impl_id=0x%08x gen_caps=0x%x", ver.impl_id, ver.gen_caps);

	memset(&arg, 0, sizeof(arg));
	arg.func = WOLFTRUST_FN_HSM_CANCEL;
	rc = tee_invoke_func(tee, &arg, 0, NULL);
	LOG_INF("tee_invoke_func(cancel) rc=%d ret=0x%x", rc, arg.ret);
}

static void exercise_psa_rng(void)
{
	uint8_t out[16];
	psa_status_t st;

	st = psa_generate_random(out, sizeof(out));
	LOG_INF("psa_generate_random st=%d first=0x%02x",
		(int)st, (unsigned)out[0]);
}

static void exercise_psa_hash(void)
{
    static const uint8_t input[] =
        "wolfTrust/wolfPSA/wolfHSM/CMSE chain test";
    static const uint8_t expected[32] = {
        0x02, 0x7b, 0x1a, 0xec, 0xb3, 0x27, 0x3a, 0x54,
        0x38, 0x6a, 0xea, 0x85, 0x66, 0x45, 0xa2, 0x6a,
        0xe1, 0xce, 0xc4, 0xdf, 0x1e, 0x00, 0x72, 0x71,
        0xab, 0x5f, 0x10, 0x21, 0x40, 0x57, 0xed, 0x67
    };
    uint8_t digest[sizeof(expected)];
    size_t digestLen = 0u;
    psa_status_t status;

    status = psa_hash_compute(PSA_ALG_SHA_256, input, sizeof(input) - 1u,
        digest, sizeof(digest), &digestLen);
    if ((status != PSA_SUCCESS) || (digestLen != sizeof(expected)) ||
            (memcmp(digest, expected, sizeof(expected)) != 0)) {
        LOG_ERR("psa_hash_compute(SHA-256) KAT failed st=%d len=%u",
            (int)status, (unsigned)digestLen);
        if ((status == PSA_SUCCESS) && (digestLen <= sizeof(digest))) {
            LOG_HEXDUMP_ERR(digest, digestLen, "SHA-256 received");
        }
        return;
    }

    LOG_INF("psa_hash_compute(SHA-256) KAT verified");
}

static void exercise_psa_cipher(void)
{
	psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = PSA_KEY_ID_NULL;
	uint8_t plaintext[16];
	uint8_t ciphertext[PSA_CIPHER_ENCRYPT_OUTPUT_SIZE(PSA_KEY_TYPE_AES,
							   PSA_ALG_CTR,
							   sizeof(plaintext))];
	size_t ct_len = 0;
	psa_status_t st;

	memset(plaintext, 0xA5, sizeof(plaintext));

	psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT |
					  PSA_KEY_USAGE_DECRYPT);
	psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_VOLATILE);
	psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
	psa_set_key_algorithm(&attr, PSA_ALG_CTR);
	psa_set_key_bits(&attr, 128);

	st = psa_generate_key(&attr, &key);
	if (st != PSA_SUCCESS) {
		LOG_WRN("psa_generate_key st=%d", (int)st);
		return;
	}

	st = psa_cipher_encrypt(key, PSA_ALG_CTR,
				 plaintext, sizeof(plaintext),
				 ciphertext, sizeof(ciphertext), &ct_len);
	LOG_INF("psa_cipher_encrypt(AES-CTR) st=%d ct_len=%u",
		(int)st, (unsigned)ct_len);

	(void)psa_destroy_key(key);
}

static void exercise_psa_initial_attestation(void)
{
    uint8_t challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32];
    uint8_t token[512];
    uint8_t publicKey[65];
    size_t tokenSize = 0u;
    size_t publicKeySize = 0u;
    psa_status_t status;
    uint32_t keyPrefixHigh;
    uint32_t keyPrefixLow;
    uint32_t verifiedLifecycle = 0u;
    int verify;
    size_t i;

    for (i = 0u; i < sizeof(challenge); ++i) {
        challenge[i] = (uint8_t)(0xA0u + i);
    }

    status = psa_initial_attest_get_token_size(sizeof(challenge), &tokenSize);
    if ((status != PSA_SUCCESS) || (tokenSize > sizeof(token))) {
        LOG_INF("psa_initial_attestation unavailable st=%d size=%u",
            (int)status, (unsigned)tokenSize);
        return;
    }

    status = psa_initial_attest_get_token(challenge, sizeof(challenge), token,
        sizeof(token), &tokenSize);
    LOG_INF("psa_initial_attestation st=%d token_len=%u", (int)status,
        (unsigned)tokenSize);
    if (status != PSA_SUCCESS) {
        return;
    }
    LOG_INF("wolfTrust attestation: wolfCOSE COSE_Sign1 signed by wolfHSM");

    status = wolftrust_attestation_get_iak_public_key(publicKey,
        sizeof(publicKey), &publicKeySize);
    if (status != PSA_SUCCESS) {
        LOG_INF("psa_initial_attestation public_key_st=%d", (int)status);
        return;
    }
    keyPrefixHigh = ((uint32_t)publicKey[1] << 24) |
        ((uint32_t)publicKey[2] << 16) |
        ((uint32_t)publicKey[3] << 8) | (uint32_t)publicKey[4];
    keyPrefixLow = ((uint32_t)publicKey[5] << 24) |
        ((uint32_t)publicKey[6] << 16) |
        ((uint32_t)publicKey[7] << 8) | (uint32_t)publicKey[8];
    LOG_INF("wolfTrust attestation: IAK public key prefix=%08x%08x",
        (unsigned)keyPrefixHigh, (unsigned)keyPrefixLow);

    verify = wt_attestation_verify(token, tokenSize, publicKey, publicKeySize,
        challenge, sizeof(challenge), WT_EXPECTED_MEASUREMENT_HEX,
        WT_EXPECTED_LIFECYCLE, &verifiedLifecycle);
    if (verify == 0) {
        LOG_INF("wolfTrust attestation: COSE_Sign1 verified");
        LOG_INF("attestation verify=0 challenge=ok identity=ok "
            "lifecycle=0x%04x measurement=ok cose=ES256",
            (unsigned)WT_EXPECTED_LIFECYCLE);
#if defined(WT_ATTESTATION_DEVELOPMENT_PROFILE)
        verify = wt_attestation_verify(token, tokenSize, publicKey,
            publicKeySize, challenge, sizeof(challenge),
            WT_EXPECTED_MEASUREMENT_HEX, WT_PSA_LIFECYCLE_SECURED,
            &verifiedLifecycle);
        if (verify == 0) {
            LOG_ERR("secured lifecycle policy accepted development token");
            return;
        }
        LOG_INF("secured lifecycle policy rejected development token");
#endif
    }
    else {
        LOG_ERR("wolfTrust attestation: COSE_Sign1 verification failed rc=%d "
            "expected_lifecycle=0x%04x received_lifecycle=0x%04x", verify,
            (unsigned)WT_EXPECTED_LIFECYCLE, (unsigned)verifiedLifecycle);
    }
}

int main(void)
{
	int rc;

	LOG_INF("guest0_psa alive");

	rc = wt_zephyr_client_init("guest0_psa");
	if (rc == 0) {
		LOG_INF("wolfTrust TEE client initialized");
	} else {
		LOG_WRN("wolfTrust TEE init rc=%d (%s)", rc,
			wt_zephyr_client_status_string(rc));
	}

	exercise_tee_driver();
	exercise_psa_rng();
	exercise_psa_hash();
	exercise_psa_cipher();
	exercise_psa_initial_attestation();

	LOG_INF("guest0_psa done");

#if defined(WT_M33MU_EXPECT_BKPT)
    __asm volatile("bkpt #0x7f");
#endif

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
