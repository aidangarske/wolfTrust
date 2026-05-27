/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Zephyr TEE driver binding for wolfTrust. Translates the standard
 * Zephyr `tee` subsystem calls into CMSE veneer calls into the
 * wolfTrust secure side.
 *
 * Only get_version is implemented for now; invoke_func is wired to
 * the wolfHSM poll/cancel veneers that the wolfTrust secure side
 * already exposes.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/tee.h>

#define DT_DRV_COMPAT wolfssl_wolftrust_tee

/* "WTRT" — wolfTrust impl id, returned via get_version. */
#define TEE_IMPL_ID_WOLFTRUST 0x57545254u

/* Function IDs the NS side can invoke via tee_invoke_func. Keep these
 * small and stable; the secure side maps them to wolfHSM veneers. */
#define WOLFTRUST_FN_HSM_POLL   1u
#define WOLFTRUST_FN_HSM_CANCEL 2u

extern int WolfTrust_HSM_Poll(uint16_t seq);
extern int WolfTrust_HSM_Cancel(uint16_t seq);

static int wolftrust_get_version(const struct device *dev,
				 struct tee_version_info *info)
{
	ARG_UNUSED(dev);

	if (info == NULL) {
		return -EINVAL;
	}

	info->impl_id = TEE_IMPL_ID_WOLFTRUST;
	info->impl_caps = 0;
	info->gen_caps = TEE_GEN_CAP_GP;
	return 0;
}

static int wolftrust_invoke_func(const struct device *dev,
				 struct tee_invoke_func_arg *arg,
				 unsigned int num_param,
				 struct tee_param *param)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(num_param);
	ARG_UNUSED(param);

	if (arg == NULL) {
		return -EINVAL;
	}

	switch (arg->func) {
	case WOLFTRUST_FN_HSM_POLL:
		arg->ret = (uint32_t)WolfTrust_HSM_Poll(0u);
		break;
	case WOLFTRUST_FN_HSM_CANCEL:
		arg->ret = (uint32_t)WolfTrust_HSM_Cancel(0u);
		break;
	default:
		arg->ret = (uint32_t)-ENOSYS;
		return -ENOSYS;
	}
	arg->ret_origin = TEEC_ORIGIN_TRUSTED_APP;
	return 0;
}

static const struct tee_driver_api wolftrust_tee_api = {
	.get_version = wolftrust_get_version,
	.invoke_func = wolftrust_invoke_func,
};

static int wolftrust_tee_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	/* Probe the secure side. Cancel(0) is a soft no-op that returns
	 * WH_ERROR_OK; any other value means the veneer is unreachable. */
	return WolfTrust_HSM_Cancel(0u) == 0 ? 0 : -EIO;
}

#define WOLFTRUST_TEE_INST(inst)                                              \
	DEVICE_DT_INST_DEFINE(inst, wolftrust_tee_init, NULL, NULL, NULL,     \
			      POST_KERNEL,                                    \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,             \
			      &wolftrust_tee_api);

DT_INST_FOREACH_STATUS_OKAY(WOLFTRUST_TEE_INST)
