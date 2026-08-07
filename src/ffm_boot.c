/* ffm_boot.c
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

#include "wolftrust/ffm_boot.h"

#include "wolftrust/arch/armv8m/cmse.h"
#include "wolftrust/ffm_api.h"
#include "wolftrust/monitor.h"

static wt_ffm_runtime_t g_ffm_runtime;

/* WT-FFM-0012: the SPM validates every external memory reference before
 * an API transfer. Every registered service today declares
 * nonsecure_clients, so the only caller identity this port validates is a
 * Non-secure guest (caller < 0). Secure-Partition callers (caller > 0)
 * have no memory-envelope check yet and stay fail-closed until item 5
 * gives each SP its own L3 domain. */
static int wt_ffm_boot_caller_guest(psa_client_id_t caller,
                                    wt_guest_id_t* guest_id)
{
    if (caller >= 0) {
        return 0;
    }
    *guest_id = (wt_guest_id_t)(-caller - 1);
    return 1;
}

static int wt_ffm_boot_check_read(void* context, psa_client_id_t caller,
                                  const void* address, size_t size)
{
    wt_guest_id_t guest_id;

    (void)context;
    if (!wt_ffm_boot_caller_guest(caller, &guest_id)) {
        return 0;
    }
    return wt_cmse_check_ns_ro(address, size) &&
           wt_cmse_check_in_guest_ns_addr(guest_id, address, size);
}

static int wt_ffm_boot_check_write(void* context, psa_client_id_t caller,
                                   void* address, size_t size)
{
    wt_guest_id_t guest_id;

    (void)context;
    if (!wt_ffm_boot_caller_guest(caller, &guest_id)) {
        return 0;
    }
    return wt_cmse_check_ns_rw(address, size) &&
           wt_cmse_check_in_guest_ns_ram(guest_id, address, size);
}

/* No Secure Partition service loop runs yet; item 3 Section C wires the
 * first real service (crypto) through this dispatch path. */
static int wt_ffm_boot_dispatch(void* context, wt_ffm_runtime_t* runtime,
                                int32_t partition_id)
{
    (void)context;
    (void)runtime;
    (void)partition_id;
    return WT_FFM_ERROR_STATE;
}

static void wt_ffm_boot_panic(void* context, int32_t partition_id)
{
    (void)context;
    (void)partition_id;
    wt_platform_panic();
}

static const wt_ffm_port_ops_t g_ffm_port_ops = {
    wt_ffm_boot_check_read,
    wt_ffm_boot_check_write,
    wt_ffm_boot_dispatch,
    wt_ffm_boot_panic
};

static psa_client_id_t wt_ffm_boot_current_client(void* context)
{
    const wt_scheduler_state_t* state;

    (void)context;
    state = wt_monitor_state();
    return -(psa_client_id_t)(state->current_guest + 1U);
}

/* No Secure Partition context is scheduled yet; item 3 Section C tracks
 * the active partition once a real service loop runs. */
static int32_t wt_ffm_boot_current_partition(void* context)
{
    (void)context;
    return 0;
}

static const wt_ffm_identity_ops_t g_ffm_identity_ops = {
    wt_ffm_boot_current_client,
    wt_ffm_boot_current_partition
};

int wt_ffm_boot_init(const wt_system_manifest_t* manifest)
{
    int ret;

    ret = wt_ffm_init(&g_ffm_runtime, manifest, &g_ffm_port_ops, NULL);
    if (ret == WT_FFM_SUCCESS) {
        ret = wt_ffm_api_bind(&g_ffm_runtime, &g_ffm_identity_ops, NULL);
    }
    return ret;
}

const wt_ffm_runtime_t* wt_ffm_boot_runtime(void)
{
    return &g_ffm_runtime;
}
