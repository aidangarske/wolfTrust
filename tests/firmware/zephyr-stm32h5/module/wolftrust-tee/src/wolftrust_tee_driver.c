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
 * Only get_version is implemented for now; invoke_func is wired to
 * the wolfHSM poll/cancel veneers that the wolfTrust secure side
 * already exposes.
 */

#include <errno.h>
#include <stdint.h>

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

extern int WolfTrust_HSM_Poll(uint16_t seq);
extern int WolfTrust_HSM_Cancel(uint16_t seq);
extern int32_t WolfTrust_FFM_Connect(uint32_t sid, uint32_t version);
extern int32_t WolfTrust_FFM_Call(int32_t handle, int32_t type,
                                  const wt_ffm_veneer_iovec_t* ns_iovec);
extern void WolfTrust_FFM_Close(int32_t handle);

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
	wt_ffm_veneer_iovec_t iovec;

	ARG_UNUSED(dev);

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
	case WOLFTRUST_FN_FFM_CONNECT:
		if (num_param < 1) {
			arg->ret = (uint32_t)-EINVAL;
			return -EINVAL;
		}
		arg->ret = (uint32_t)WolfTrust_FFM_Connect(
			(uint32_t)param[0].a, (uint32_t)param[0].b);
		break;
	case WOLFTRUST_FN_FFM_CALL:
		/* param[0] = {handle, type, input_ptr},
		 * param[1] = {input_len, output_ptr, output_len}. */
		if (num_param < 2) {
			arg->ret = (uint32_t)-EINVAL;
			return -EINVAL;
		}
		iovec.input = (const void *)(uintptr_t)param[0].c;
		iovec.input_len = (uint32_t)param[1].a;
		iovec.output = (void *)(uintptr_t)param[1].b;
		iovec.output_len = (uint32_t)param[1].c;
		arg->ret = (uint32_t)WolfTrust_FFM_Call(
			(int32_t)param[0].a, (int32_t)param[0].b, &iovec);
		break;
	case WOLFTRUST_FN_FFM_CLOSE:
		if (num_param < 1) {
			arg->ret = (uint32_t)-EINVAL;
			return -EINVAL;
		}
		WolfTrust_FFM_Close((int32_t)param[0].a);
		arg->ret = 0;
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
