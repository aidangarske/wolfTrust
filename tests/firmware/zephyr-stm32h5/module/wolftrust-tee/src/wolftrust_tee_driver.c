/* wolftrust_tee_driver.c
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

/* Zephyr TEE driver binding for wolfTrust. Translates the standard
 * Zephyr `tee` subsystem calls into CMSE veneer calls into the
 * wolfTrust secure side.
 *
 * Only get_version is implemented for now; invoke_func's poll/cancel
 * probes ride the mediated FF-M gateway veneer (WT-FFM-0054) — the raw
 * WolfTrust_HSM_* transport is retired.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/tee.h>

#include <wolftrust/ffm_veneer.h>

#define DT_DRV_COMPAT wolfssl_wolftrust_tee

/* "WTRT" — wolfTrust impl id, returned via get_version. */
#define TEE_IMPL_ID_WOLFTRUST 0x57545254u

/* Function IDs the NS side can invoke via tee_invoke_func. Keep these
 * small and stable; the secure side maps them to wolfHSM veneers. */
#define WOLFTRUST_FN_HSM_POLL   1u
#define WOLFTRUST_FN_HSM_CANCEL 2u

/* Item 3c: stopgap NS-to-Secure carrier for wolfTrust's own FF-M client
 * API, reusing this TEE transport until purpose-built FF-M veneers exist
 * (task-list.md item 3c-followup). param[0].a/b/c carry scalar value
 * parameters, not real Zephyr shared-memory memrefs — the secure veneers
 * do their own CMSE validation of any pointer that crosses. */
#define WOLFTRUST_FN_FFM_CONNECT 3u
#define WOLFTRUST_FN_FFM_CALL    4u
#define WOLFTRUST_FN_FFM_CLOSE   5u

extern uint32_t WolfTrust_FFM_FrameworkVersion(void);

/* The FF-M client API (psa_connect/call/close/framework_version/version) now
 * lives in the OS-neutral src/client/psa_ffm_client.c; the guest no longer
 * routes FF-M through this TEE driver (P7-S2, closes #16 for the FF-M path).
 * The poll/cancel function ids remain as SPM liveness probes. */

static int wolftrust_spm_alive(void)
{
	return WolfTrust_FFM_FrameworkVersion() == 0x0100u ? 0 : -EIO;
}

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
	case WOLFTRUST_FN_HSM_CANCEL:
		arg->ret = (uint32_t)wolftrust_spm_alive();
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
	return wolftrust_spm_alive();
}

#define WOLFTRUST_TEE_INST(inst)                                              \
	DEVICE_DT_INST_DEFINE(inst, wolftrust_tee_init, NULL, NULL, NULL,     \
			      POST_KERNEL,                                    \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,             \
			      &wolftrust_tee_api);

DT_INST_FOREACH_STATUS_OKAY(WOLFTRUST_TEE_INST)
