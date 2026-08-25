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
#define WT_ITS_SID    4099u
#define WT_PS_SID     4100u

#ifndef WT_EXPECTED_MEASUREMENT_HEX
#define WT_EXPECTED_MEASUREMENT_HEX ""
#endif

#ifndef WT_EXPECTED_LIFECYCLE
#define WT_EXPECTED_LIFECYCLE 0x3000u
#endif

#define WT_PSA_LIFECYCLE_SECURED 0x3000u

/* NS PSA FF-M client API, aliased to the secure veneers (P3a-2). */
extern uint32_t psa_framework_version(void);

#if defined(WT_RUN_CONFORMANCE)
/* Arm psa-arch-tests val NSPE entry (P3a-4a). */
extern int32_t val_entry(void);
#endif

/* Mirror guest0's TEE-driver smoke so the runner's existing TEE assertions
 * stay green and we don't need a second runner mode. */
static void exercise_tee_driver(void)
{
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_version_info ver;
	struct tee_invoke_func_arg arg;
	uint32_t fw;
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

	fw = psa_framework_version();
	if (fw == 0x0100u)
		LOG_INF("wolfTrust FF-M psa_framework_version=0x%04x", fw);
	else
		LOG_ERR("wolfTrust FF-M psa_framework_version unexpected=0x%04x", fw);
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

/* P4-S2: the full storage chain from a real Non-secure guest — NS ->
 * SERVICE_ITS (unprivileged SP) -> SERVICE_VAULT (gated wolfHSM backing) ->
 * flash NVM — three protection domains, every hop through the SPM gate.
 * The wire header layout mirrors wt_its_req_t (uid, flags, offset) with the
 * object data concatenated for SET. */
static void exercise_ffm_its(void)
{
	static const uint8_t payload[] = "wolfTrust ITS on-target probe";
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_invoke_func_arg arg;
	struct tee_param param[2];
	uint8_t setbuf[16 + sizeof(payload)];
	uint8_t getbuf[sizeof(payload)];
	uint64_t uid = 0x57544954u; /* "WTIT" */
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
	param[0].a = WT_ITS_SID;
	param[0].b = 1u;
	rc = tee_invoke_func(tee, &arg, 1, param);
	handle = (int32_t)arg.ret;
	if (rc != 0 || handle <= 0) {
		LOG_ERR("FF-M psa_connect(SERVICE_ITS) failed rc=%d handle=%d",
			rc, handle);
		return;
	}

	memset(setbuf, 0, sizeof(setbuf));
	memcpy(setbuf, &uid, sizeof(uid));
	memcpy(setbuf + 16, payload, sizeof(payload));
	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)handle;
	param[0].b = 1u; /* WT_ITS_OP_SET */
	param[0].c = (uint64_t)(uintptr_t)setbuf;
	param[1].a = sizeof(setbuf);
	rc = tee_invoke_func(tee, &arg, 2, param);
	st = (int32_t)arg.ret;
	if (rc != 0 || st != 0) {
		LOG_ERR("psa_its_set via SERVICE_ITS failed rc=%d st=%d",
			rc, st);
	} else {
		memset(getbuf, 0, sizeof(getbuf));
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 2u; /* WT_ITS_OP_GET */
		param[0].c = (uint64_t)(uintptr_t)setbuf;
		param[1].a = 16u; /* header only */
		param[1].b = (uint64_t)(uintptr_t)getbuf;
		param[1].c = sizeof(getbuf);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != 0) {
			LOG_ERR("psa_its_get via SERVICE_ITS failed rc=%d "
				"st=%d", rc, st);
		} else if (memcmp(getbuf, payload, sizeof(payload)) != 0) {
			LOG_ERR("wolfTrust ITS get returned wrong data");
		} else {
			LOG_INF("wolfTrust ITS set/get verified");
		}
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CLOSE;
	param[0].a = (uint64_t)handle;
	(void)tee_invoke_func(tee, &arg, 1, param);
}

/* P4-S3: the sealed-storage round trip. Same wire as ITS, but SERVICE_PS
 * AES-GCM-seals every object inside the privileged vault domain, so a clean
 * set/get proves seal + rollback-counter + unseal end to end on target. */
static void exercise_ffm_ps(void)
{
	static const uint8_t payload[] = "wolfTrust PS on-target secret";
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_invoke_func_arg arg;
	struct tee_param param[2];
	uint8_t setbuf[16 + sizeof(payload)];
	uint8_t getbuf[sizeof(payload)];
	uint64_t uid = 0x57545053u; /* "WTPS" */
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
	param[0].a = WT_PS_SID;
	param[0].b = 1u;
	rc = tee_invoke_func(tee, &arg, 1, param);
	handle = (int32_t)arg.ret;
	if (rc != 0 || handle <= 0) {
		LOG_ERR("FF-M psa_connect(SERVICE_PS) failed rc=%d handle=%d",
			rc, handle);
		return;
	}

	memset(setbuf, 0, sizeof(setbuf));
	memcpy(setbuf, &uid, sizeof(uid));
	memcpy(setbuf + 16, payload, sizeof(payload));
	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)handle;
	param[0].b = 1u; /* WT_ITS_OP_SET */
	param[0].c = (uint64_t)(uintptr_t)setbuf;
	param[1].a = sizeof(setbuf);
	rc = tee_invoke_func(tee, &arg, 2, param);
	st = (int32_t)arg.ret;
	if (rc != 0 || st != 0) {
		LOG_ERR("psa_ps_set via SERVICE_PS failed rc=%d st=%d",
			rc, st);
	} else {
		memset(getbuf, 0, sizeof(getbuf));
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 2u; /* WT_ITS_OP_GET */
		param[0].c = (uint64_t)(uintptr_t)setbuf;
		param[1].a = 16u; /* header only */
		param[1].b = (uint64_t)(uintptr_t)getbuf;
		param[1].c = sizeof(getbuf);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != 0) {
			LOG_ERR("psa_ps_get via SERVICE_PS failed rc=%d "
				"st=%d", rc, st);
		} else if (memcmp(getbuf, payload, sizeof(payload)) != 0) {
			LOG_ERR("wolfTrust PS get returned wrong data");
		} else {
			LOG_INF("wolfTrust PS sealed set/get verified");
		}
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CLOSE;
	param[0].a = (uint64_t)handle;
	(void)tee_invoke_func(tee, &arg, 1, param);
}

/* P4-S4: vault key-ops through SERVICE_CRYPTO. The key never exists outside
 * the privileged vault domain — this probe proves generate, export_public,
 * sign, verify and a tampered-digest refusal end to end on target. The
 * leading destroy keeps the probe idempotent on hardware, where the NVM
 * persists across runs. */
static void exercise_ffm_keys(void)
{
	static const uint8_t digest[32] = {
		0x57, 0x54, 0x4B, 0x56, 0x01, 0x02, 0x03, 0x04,
		0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
		0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
		0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C
	};
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_invoke_func_arg arg;
	struct tee_param param[2];
	uint8_t req[16 + 96];
	uint8_t pub[65];
	uint8_t sig[64];
	uint64_t uid = 0x57544B56u; /* "WTKV" */
	uint32_t usage = 0x3u;      /* SIGN | VERIFY */
	uint32_t key_type = 1u;     /* P-256 */
	int32_t handle;
	int32_t st;
	int rc;
	int ok = 1;

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
		LOG_ERR("FF-M psa_connect(SERVICE_CRYPTO keys) failed rc=%d "
			"handle=%d", rc, handle);
		return;
	}

	memset(req, 0, sizeof(req));
	memcpy(req, &uid, sizeof(uid));
	memcpy(req + 8, &usage, sizeof(usage));
	memcpy(req + 12, &key_type, sizeof(key_type));

	/* Idempotence on persistent NVM: a stale key from a prior run is
	 * removed first; DOES_NOT_EXIST on first boot is expected. */
	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)handle;
	param[0].b = 8u; /* WT_CRYPTO_OP_KEY_DESTROY */
	param[0].c = (uint64_t)(uintptr_t)req;
	param[1].a = 16u;
	(void)tee_invoke_func(tee, &arg, 2, param);

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)handle;
	param[0].b = 1u; /* WT_CRYPTO_OP_KEY_GENERATE */
	param[0].c = (uint64_t)(uintptr_t)req;
	param[1].a = 16u;
	rc = tee_invoke_func(tee, &arg, 2, param);
	st = (int32_t)arg.ret;
	if (rc != 0 || st != 0) {
		LOG_ERR("key generate failed rc=%d st=%d", rc, st);
		ok = 0;
	}

	if (ok) {
		memset(pub, 0, sizeof(pub));
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 3u; /* WT_CRYPTO_OP_KEY_EXPORT_PUBLIC */
		param[0].c = (uint64_t)(uintptr_t)req;
		param[1].a = 16u;
		param[1].b = (uint64_t)(uintptr_t)pub;
		param[1].c = sizeof(pub);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != 0 || pub[0] != 0x04u) {
			LOG_ERR("export_public failed rc=%d st=%d", rc, st);
			ok = 0;
		}
	}

	if (ok) {
		memcpy(req + 16, digest, sizeof(digest));
		memset(sig, 0, sizeof(sig));
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 4u; /* WT_CRYPTO_OP_KEY_SIGN */
		param[0].c = (uint64_t)(uintptr_t)req;
		param[1].a = 16u + sizeof(digest);
		param[1].b = (uint64_t)(uintptr_t)sig;
		param[1].c = sizeof(sig);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != 0) {
			LOG_ERR("key sign failed rc=%d st=%d", rc, st);
			ok = 0;
		}
	}

	if (ok) {
		memcpy(req + 16, digest, sizeof(digest));
		memcpy(req + 16 + sizeof(digest), sig, sizeof(sig));
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 5u; /* WT_CRYPTO_OP_KEY_VERIFY */
		param[0].c = (uint64_t)(uintptr_t)req;
		param[1].a = 16u + sizeof(digest) + sizeof(sig);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != 0) {
			LOG_ERR("key verify failed rc=%d st=%d", rc, st);
			ok = 0;
		}
	}

	if (ok) {
		req[16] ^= 0x01u; /* corrupt the digest */
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 5u; /* WT_CRYPTO_OP_KEY_VERIFY */
		param[0].c = (uint64_t)(uintptr_t)req;
		param[1].a = 16u + sizeof(digest) + sizeof(sig);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != -149) {
			LOG_ERR("tampered verify not refused rc=%d st=%d",
				rc, st);
			ok = 0;
		}
	}

	if (ok) {
		LOG_INF("wolfTrust key-ops sign/verify verified");
	}

	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CLOSE;
	param[0].a = (uint64_t)handle;
	(void)tee_invoke_func(tee, &arg, 1, param);
}

/* P4-S5 on-target negative (WT-FFM-0046): a wrong-key AES-GCM decrypt fails
 * authentication — there is no cross-key oracle. Two AES keys are generated
 * in the vault; ciphertext produced under key A cannot be decrypted under
 * key B (st = INVALID_SIGNATURE), while key A still decrypts its own. This
 * is also the first on-target exercise of the key encrypt/decrypt path. */
static void exercise_ffm_key_negatives(void)
{
	static const uint8_t msg_pt[] = "wolfTrust key negative probe";
	const struct device *tee = DEVICE_DT_GET_ANY(wolfssl_wolftrust_tee);
	struct tee_invoke_func_arg arg;
	struct tee_param param[2];
	uint8_t req[16 + 128];
	uint8_t ct[sizeof(msg_pt) + 28];
	uint8_t pt[sizeof(msg_pt)];
	uint64_t uid_a = 0x4E454741u; /* "NEGA" */
	uint64_t uid_b = 0x4E454742u; /* "NEGB" */
	uint32_t usage = 0xCu;   /* ENCRYPT | DECRYPT */
	uint32_t key_type = 2u;  /* AES-256 */
	int32_t handle;
	int32_t st;
	int rc;
	int ok = 1;
	uint32_t ct_len;

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
		LOG_ERR("FF-M psa_connect(SERVICE_CRYPTO negatives) failed rc=%d "
			"handle=%d", rc, handle);
		return;
	}

	/* Generate both AES keys (destroy-first for hardware idempotence). */
	memset(req, 0, sizeof(req));
	memcpy(req + 8, &usage, sizeof(usage));
	memcpy(req + 12, &key_type, sizeof(key_type));
	memcpy(req, &uid_a, sizeof(uid_a));
	memset(&arg, 0, sizeof(arg));
	memset(param, 0, sizeof(param));
	arg.func = WOLFTRUST_FN_FFM_CALL;
	param[0].a = (uint64_t)handle;
	param[0].b = 8u; /* DESTROY */
	param[0].c = (uint64_t)(uintptr_t)req;
	param[1].a = 16u;
	(void)tee_invoke_func(tee, &arg, 2, param);
	param[0].b = 1u; /* GENERATE */
	rc = tee_invoke_func(tee, &arg, 2, param);
	if (rc != 0 || (int32_t)arg.ret != 0) {
		ok = 0;
	}
	memcpy(req, &uid_b, sizeof(uid_b));
	param[0].b = 8u; /* DESTROY */
	(void)tee_invoke_func(tee, &arg, 2, param);
	param[0].b = 1u; /* GENERATE */
	rc = tee_invoke_func(tee, &arg, 2, param);
	if (rc != 0 || (int32_t)arg.ret != 0) {
		ok = 0;
	}

	/* Encrypt under key A. */
	ct_len = 0u;
	if (ok) {
		memcpy(req, &uid_a, sizeof(uid_a));
		memcpy(req + 16, msg_pt, sizeof(msg_pt));
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 6u; /* ENCRYPT */
		param[0].c = (uint64_t)(uintptr_t)req;
		param[1].a = 16u + sizeof(msg_pt);
		param[1].b = (uint64_t)(uintptr_t)ct;
		param[1].c = sizeof(ct);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		ct_len = (uint32_t)param[1].c;
		if (rc != 0 || st != 0) {
			LOG_ERR("negatives encrypt failed rc=%d st=%d", rc, st);
			ok = 0;
		}
	}

	/* Decrypt under key B must fail authentication (-149). */
	if (ok) {
		memcpy(req, &uid_b, sizeof(uid_b));
		memcpy(req + 16, ct, ct_len);
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 7u; /* DECRYPT */
		param[0].c = (uint64_t)(uintptr_t)req;
		param[1].a = 16u + ct_len;
		param[1].b = (uint64_t)(uintptr_t)pt;
		param[1].c = sizeof(pt);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != -149) {
			LOG_ERR("wrong-key decrypt not refused rc=%d st=%d",
				rc, st);
			ok = 0;
		}
	}

	/* Decrypt under key A must succeed and round-trip. */
	if (ok) {
		memcpy(req, &uid_a, sizeof(uid_a));
		memcpy(req + 16, ct, ct_len);
		memset(pt, 0, sizeof(pt));
		memset(&arg, 0, sizeof(arg));
		memset(param, 0, sizeof(param));
		arg.func = WOLFTRUST_FN_FFM_CALL;
		param[0].a = (uint64_t)handle;
		param[0].b = 7u; /* DECRYPT */
		param[0].c = (uint64_t)(uintptr_t)req;
		param[1].a = 16u + ct_len;
		param[1].b = (uint64_t)(uintptr_t)pt;
		param[1].c = sizeof(pt);
		rc = tee_invoke_func(tee, &arg, 2, param);
		st = (int32_t)arg.ret;
		if (rc != 0 || st != 0 ||
		    memcmp(pt, msg_pt, sizeof(msg_pt)) != 0) {
			LOG_ERR("right-key decrypt failed rc=%d st=%d", rc, st);
			ok = 0;
		}
	}

	if (ok) {
		LOG_INF("wolfTrust key negatives verified");
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
    uint8_t tokenMeasurement[32];
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

    verify = wt_attestation_verify_ex(token, tokenSize, publicKey,
        publicKeySize, challenge, sizeof(challenge),
        WT_EXPECTED_MEASUREMENT_HEX, WT_EXPECTED_LIFECYCLE,
        &verifiedLifecycle, tokenMeasurement);
    if (verify == 0) {
        static const char hexDigits[] = "0123456789abcdef";
        char measurementHex[65];
        unsigned int hexIndex;

        for (hexIndex = 0u; hexIndex < 32u; hexIndex++) {
            measurementHex[hexIndex * 2u] =
                hexDigits[(tokenMeasurement[hexIndex] >> 4) & 0x0Fu];
            measurementHex[(hexIndex * 2u) + 1u] =
                hexDigits[tokenMeasurement[hexIndex] & 0x0Fu];
        }
        measurementHex[64] = '\0';
        LOG_INF("wolfTrust attestation: COSE_Sign1 verified");
        LOG_INF("wolfTrust attestation: token measurement=%s", measurementHex);
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

#if defined(WT_ATTEST_NEG_PROBE)
/* Attestation negatives over the real FF-M IPC path (P5-CI attestneg): the
 * secure side must reject invalid requests with the PSA statuses ARM's
 * test_a001 depends on, and a tampered or misattributed token must fail the
 * guest verify. */
static void exercise_attestation_negatives(void)
{
    uint8_t challenge[PSA_INITIAL_ATTEST_CHALLENGE_SIZE_64 + 1u];
    uint8_t token[512];
    uint8_t publicKey[65];
    size_t tokenSize = 0u;
    size_t publicKeySize = 0u;
    size_t querySize = 0u;
    uint32_t verifiedLifecycle = 0u;
    psa_status_t status;
    int verify;
    size_t i;

    for (i = 0u; i < sizeof(challenge); ++i) {
        challenge[i] = (uint8_t)(0xC0u + i);
    }

    status = psa_initial_attest_get_token_size(sizeof(challenge), &querySize);
    if (status != PSA_ERROR_INVALID_ARGUMENT) {
        LOG_ERR("attestneg oversized challenge not rejected st=%d",
            (int)status);
        return;
    }
    LOG_INF("attestneg oversized challenge rejected st=%d", (int)status);

    status = psa_initial_attest_get_token(challenge,
        PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32, token, 0u, &tokenSize);
    if (status != PSA_ERROR_INVALID_ARGUMENT) {
        LOG_ERR("attestneg zero token buffer not rejected st=%d",
            (int)status);
        return;
    }
    LOG_INF("attestneg zero token buffer rejected st=%d", (int)status);

    status = psa_initial_attest_get_token(challenge,
        PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32, token, sizeof(token),
        &tokenSize);
    if (status != PSA_SUCCESS) {
        LOG_ERR("attestneg baseline token failed st=%d", (int)status);
        return;
    }
    status = wolftrust_attestation_get_iak_public_key(publicKey,
        sizeof(publicKey), &publicKeySize);
    if (status != PSA_SUCCESS) {
        LOG_ERR("attestneg public key fetch failed st=%d", (int)status);
        return;
    }

    token[tokenSize - 1u] ^= 0x01u;
    verify = wt_attestation_verify(token, tokenSize, publicKey, publicKeySize,
        challenge, PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32,
        WT_EXPECTED_MEASUREMENT_HEX, WT_EXPECTED_LIFECYCLE,
        &verifiedLifecycle);
    if (verify == 0) {
        LOG_ERR("attestneg tampered token accepted");
        return;
    }
    token[tokenSize - 1u] ^= 0x01u;
    LOG_INF("attestneg tampered token rejected");

    verify = wt_attestation_verify(token, tokenSize, publicKey, publicKeySize,
        challenge, PSA_INITIAL_ATTEST_CHALLENGE_SIZE_32,
        WT_EXPECTED_MEASUREMENT_HEX, 0xEEEEu, &verifiedLifecycle);
    if (verify == 0) {
        LOG_ERR("attestneg lifecycle mismatch accepted");
        return;
    }
    LOG_INF("attestneg lifecycle mismatch rejected");

    LOG_INF("wolfTrust attestation negatives verified");
}
#endif

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
	exercise_ffm_its();
	exercise_ffm_ps();
	exercise_ffm_keys();
	exercise_ffm_key_negatives();
	exercise_ffm_negatives();
#if !defined(WT_RUN_CONFORMANCE)
	/* The COSE attestation path needs a deep stack; skip it in the conformance
	 * guest so the Arm val NSPE framework fits guest0's 32 KiB NS window. The
	 * full lifecycle is covered by the positive scenario. */
	exercise_psa_initial_attestation();
#if defined(WT_ATTEST_NEG_PROBE)
	exercise_attestation_negatives();
#endif
	exercise_psa_rng();
	exercise_psa_hash();
	exercise_psa_cipher();
#endif

#if defined(WT_RUN_CONFORMANCE)
	LOG_INF("wolfTrust FF-M conformance: val_entry start");
	(void)val_entry();
#endif

	LOG_INF("guest0_psa done");

#if defined(WT_M33MU_EXPECT_BKPT)
    __asm volatile("bkpt #0x7f");
#endif

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
