/* guest0_psa — minimal PSA Crypto smoke for the wolfPSA + wolfHSM chain.
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

#include <wolftrust/zephyr/client.h>

LOG_MODULE_REGISTER(guest0_psa, LOG_LEVEL_INF);

#define WOLFTRUST_FN_HSM_CANCEL 2u

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
	uint8_t digest[32];
	size_t digest_len = 0;
	psa_status_t st;

	st = psa_hash_compute(PSA_ALG_SHA_256,
			      input, sizeof(input) - 1,
			      digest, sizeof(digest), &digest_len);
	LOG_INF("psa_hash_compute(SHA-256) st=%d len=%u first=0x%02x",
		(int)st, (unsigned)digest_len, (unsigned)digest[0]);
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

	LOG_INF("guest0_psa done");

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
