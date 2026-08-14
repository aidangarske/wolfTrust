/* conf_nvm_sync.c
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

/* Target side of the survive-reset NVM seam (P5 K2): trap the DRIVER
 * partition's shadow buffer through the same SVC #1 the psa_* veneers use, so
 * the privileged SPM can drive the flash controller. The host test provides a
 * flash-simulator implementation of wt_conf_nvm_sync instead of this file. */

#include "conf_nvm.h"

#include "wolftrust/arch/armv8m/spm_svc.h"
#include "wolftrust/spm_gate.h"

#include <string.h>

int wt_conf_nvm_sync(uint8_t *buf, uint32_t len, int store)
{
    wt_spm_call_t call;

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_CONF_NVM_SYNC;
    call.buffer = buf;
    call.num_bytes = (size_t)len;
    call.call_type = store;

    (void)wt_spm_sp_call(&call);
    return call.ret_int;
}
