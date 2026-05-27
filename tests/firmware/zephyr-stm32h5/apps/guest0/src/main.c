#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/tee.h>
#include <zephyr/logging/log.h>

#include <wolftrust/zephyr/client.h>

LOG_MODULE_REGISTER(wolftrust_guest0, LOG_LEVEL_INF);

#define WOLFTRUST_FN_HSM_POLL   1u
#define WOLFTRUST_FN_HSM_CANCEL 2u

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

int main(void)
{
	int rc;

	LOG_INF("guest0 alive");
	rc = wt_zephyr_client_init("guest0");
	if (rc == 0) {
		LOG_INF("wolfTrust TEE client initialized");
	} else {
		LOG_WRN("wolfTrust TEE init rc=%d (%s)", rc,
			wt_zephyr_client_status_string(rc));
	}

	exercise_tee_driver();

	for (;;) {
		k_sleep(K_SECONDS(1));
	}
}
