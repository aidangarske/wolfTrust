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
#define WOLFTRUST_FN_FFM_CONNECT 3u
#define WOLFTRUST_FN_FFM_CALL    4u
#define WOLFTRUST_FN_FFM_CLOSE   5u
#define WT_CRYPTO_SID 4097u

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

/* Item 3c-ns: proves the FF-M dispatch path 3a/3b/3c wired up (real
 * psa_connect/psa_call servicing SERVICE_CRYPTO) end to end from a real
 * Non-secure guest, not just a host test. Reuses the existing TEE
 * transport (task-list.md item 3c-followup tracks replacing it with
 * purpose-built FF-M veneers). */
static void exercise_ffm_crypto(void)
{
	static const uint8_t input[] =
		"wolfTrust FF-M SERVICE_CRYPTO dispatch test";
	static const uint8_t expected[32] = {
		0x20, 0x03, 0xdf, 0x15, 0x2a, 0x52, 0x8a, 0x06,
		0xc8, 0xd3, 0x48, 0xb8, 0xfa, 0x8b, 0x2f, 0x87,
		0xf7, 0x1f, 0xae, 0xc6, 0x24, 0x6c, 0x7e, 0x72,
		0x8e, 0x27, 0xa4, 0xb5, 0x0a, 0x49, 0x84, 0x66
	};
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_invoke_func_arg arg;
	struct tee_param param[2];
	uint8_t digest[sizeof(expected)];
	int32_t handle;
	int rc;

	if (tee == NULL || !device_is_ready(tee)) {
		LOG_WRN("wolftrust TEE device not present/ready");
		return;
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CONNECT;
	param[0].a = WT_CRYPTO_SID;
	param[0].b = 1u;
	rc = tee_invoke_func(tee, &arg, 1, param);
	handle = (int32_t)arg.ret;
	if (rc != 0 || handle <= 0) {
		LOG_ERR("FF-M psa_connect(SERVICE_CRYPTO) failed rc=%d "
			"handle=%d", rc, handle);
		return;
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)handle;
	param[0].b = 0u; /* PSA_IPC_CALL */
	param[0].c = (uint64_t)(uintptr_t)input;
	param[1].a = sizeof(input) - 1u;
	param[1].b = (uint64_t)(uintptr_t)digest;
	param[1].c = sizeof(digest);
	rc = tee_invoke_func(tee, &arg, 2, param);
	if (rc != 0 || (int32_t)arg.ret != 0) {
		LOG_ERR("FF-M psa_call(SERVICE_CRYPTO) failed rc=%d st=%d",
			rc, (int32_t)arg.ret);
	} else if (memcmp(digest, expected, sizeof(expected)) != 0) {
		LOG_ERR("wolfTrust FF-M SERVICE_CRYPTO digest mismatch");
	} else {
		LOG_INF("wolfTrust FF-M SERVICE_CRYPTO dispatch verified");
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CLOSE;
	param[0].a = (uint64_t)handle;
	(void)tee_invoke_func(tee, &arg, 1, param);
}

/* Item 9: FF-M IPC negatives on the emulator path. A real Non-secure guest
 * makes two deliberately malformed psa_call requests through the SPM veneer and
 * asserts each is rejected without a fault or stale data — the target-side proof
 * of the handle-integrity and bounded-vector checks host-tested in
 * tests/host/ffm (WT-FFM-0021, WT-FFM-0032). Both errors are recoverable, so
 * this runs inline in the normal lifecycle. */
static void exercise_ffm_negatives(void)
{
	static const uint8_t input[] = "wolfTrust FF-M negative probe";
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_invoke_func_arg arg;
	struct tee_param param[2];
	uint8_t digest[32];
	int32_t handle;
	int32_t st;
	int rc;

	if (tee == NULL || !device_is_ready(tee)) {
		LOG_WRN("wolftrust TEE device not present/ready");
		return;
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CONNECT;
	param[0].a = WT_CRYPTO_SID;
	param[0].b = 1u;
	rc = tee_invoke_func(tee, &arg, 1, param);
	handle = (int32_t)arg.ret;
	if (rc != 0 || handle <= 0) {
		LOG_ERR("FF-M negative setup connect failed rc=%d handle=%d", rc,
			handle);
		return;
	}

	/* Forged handle: the SPM must not map it to this caller's connection. */
	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)(handle + 0x1000);
	param[0].b = 0u; /* PSA_IPC_CALL */
	param[0].c = (uint64_t)(uintptr_t)input;
	param[1].a = sizeof(input) - 1u;
	param[1].b = (uint64_t)(uintptr_t)digest;
	param[1].c = sizeof(digest);
	rc = tee_invoke_func(tee, &arg, 2, param);
	st = (int32_t)arg.ret;
	if (rc == 0 && st != 0) {
		LOG_INF("wolfTrust FF-M forged-handle call rejected st=%d", st);
	} else {
		LOG_ERR("FF-M forged-handle call NOT rejected rc=%d st=%d", rc, st);
	}

	/* Oversized input vector: length beyond WT_FFM_TRANSFER_BYTES (1024) is
	 * refused on validation, before any copy. */
	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)handle;
	param[0].b = 0u; /* PSA_IPC_CALL */
	param[0].c = (uint64_t)(uintptr_t)input;
	param[1].a = 2048u; /* > WT_FFM_TRANSFER_BYTES */
	param[1].b = (uint64_t)(uintptr_t)digest;
	param[1].c = sizeof(digest);
	rc = tee_invoke_func(tee, &arg, 2, param);
	st = (int32_t)arg.ret;
	if (rc == 0 && st != 0) {
		LOG_INF("wolfTrust FF-M oversized-vector call rejected st=%d", st);
	} else {
		LOG_ERR("FF-M oversized-vector call NOT rejected rc=%d st=%d", rc, st);
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CLOSE;
	param[0].a = (uint64_t)handle;
	(void)tee_invoke_func(tee, &arg, 1, param);
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
    uint8_t undersizedToken[1];
    uint8_t publicKey[65];
    size_t tokenSize = 0u;
    size_t undersizedTokenSize = sizeof(undersizedToken);
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

    status = psa_initial_attest_get_token(challenge, sizeof(challenge),
        undersizedToken, sizeof(undersizedToken), &undersizedTokenSize);
    if (status != PSA_ERROR_BUFFER_TOO_SMALL) {
        LOG_ERR("psa_initial_attestation short-buffer mapping failed st=%d",
            (int)status);
        return;
    }
    LOG_INF("psa_initial_attestation short-buffer rejected correctly");

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

#if defined(WT_GUEST_FAULT_PROBE)
/* Test-only restart probe: a Non-secure read of Secure RAM raises a SecureFault
 * that escalates to the wolfTrust monitor, exercising the manifest restart_limit
 * on target. Immediate logging flushes the banner before the fault. */
static void wt_guest_fault_probe(void)
{
	volatile const uint32_t *secure_ram = (volatile const uint32_t *)0x30028000u;
	uint32_t sink;

	sink = *secure_ram;
	(void)sink;
}
#endif

int main(void)
{
	int rc;

	LOG_INF("guest0_psa alive");

#if defined(WT_GUEST_FAULT_PROBE)
	wt_guest_fault_probe();
#endif

	rc = wt_zephyr_client_init("guest0_psa");
	if (rc == 0) {
		LOG_INF("wolfTrust TEE client initialized");
	} else {
		LOG_WRN("wolfTrust TEE init rc=%d (%s)", rc,
			wt_zephyr_client_status_string(rc));
	}

	exercise_tee_driver();
	exercise_ffm_crypto();
	exercise_ffm_negatives();
	exercise_psa_initial_attestation();
	exercise_psa_rng();
	exercise_psa_hash();
	exercise_psa_cipher();

	LOG_INF("guest0_psa done");

#if defined(WT_M33MU_EXPECT_BKPT)
    __asm volatile("bkpt #0x7f");
#endif

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
