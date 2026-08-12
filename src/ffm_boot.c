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
#include "wolftrust/arch/armv8m/spm_svc.h"
#include "wolftrust/monitor.h"
#include "wolftrust/services/crypto_service.h"
#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
#include "wolftrust/services/attestation_service.h"
#endif
#include "psa_manifest/pid.h"

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

/* Fail-closed fallback for a partition with no registered service loop.
 * Services bind their dispatch handler through wt_ffm_register_partition in
 * wt_ffm_boot_init, so the manifest-bound partition table routes each message
 * rather than a per-PID branch here. */
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

/* The direct src/ffm_api.c psa_* binding is host-only: on target every
 * psa_* call from a Secure Partition crosses the SVC gate instead
 * (src/arch/armv8m/spm_sp_api.c), so nothing binds the identity ops here. */
int wt_ffm_boot_init(const wt_system_manifest_t* manifest)
{
    int ret;

    ret = wt_ffm_init(&g_ffm_runtime, manifest, &g_ffm_port_ops, NULL);
    if (ret == WT_FFM_SUCCESS) {
        ret = wt_ffm_register_partition(&g_ffm_runtime, PARTITION_CRYPTO_ID,
                                        wt_crypto_service_dispatch, NULL);
    }
#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
    if (ret == WT_FFM_SUCCESS) {
        ret = wt_ffm_register_partition(&g_ffm_runtime, PARTITION_ATTEST_ID,
                                        wt_attestation_service_dispatch, NULL);
    }
#endif
    if (ret == WT_FFM_SUCCESS) {
        /* Run SERVICE_CRYPTO's compute isolated on the crypto SP's own
         * secure stack under a narrowed MPU domain (WT-FFM-0011). The
         * scheduled path (wt_ffm_boot_start_sched) supersedes this once
         * the coroutine scheduler is up. */
        wt_crypto_service_set_compute(wt_platform_run_crypto_sp_isolated);
    }
    return ret;
}

#if defined(WT_CONFORMANCE) && (WT_CONFORMANCE == 1)
/* Arm PSA-FF conformance partitions (P3a): the unmodified upstream service
 * loops, scheduled like any other SP. */
extern void server_main(void);
extern void client_main(void);

static void wt_conformance_server_entry(void* arg)
{
    (void)arg;
    server_main();
}

static void wt_conformance_client_entry(void* arg)
{
    (void)arg;
    client_main();
}
#endif

int wt_ffm_boot_start_sched(void)
{
    int ret;

    ret = wt_spm_sched_start(&g_ffm_runtime, PARTITION_CRYPTO_ID);
#if defined(WT_CONFORMANCE) && (WT_CONFORMANCE == 1)
    if (ret == WT_FFM_SUCCESS) {
        ret = wt_spm_sched_add(&g_ffm_runtime, SERVER_PARTITION_ID,
                               wt_conformance_server_entry, NULL);
    }
    if (ret == WT_FFM_SUCCESS) {
        ret = wt_spm_sched_add(&g_ffm_runtime, CLIENT_PARTITION_ID,
                               wt_conformance_client_entry, NULL);
    }
#endif
    return ret;
}

/* Stopgap NS-to-Secure carrier for the FF-M client API (see task-list.md
 * item 3c): the Zephyr wolftrust-tee driver's tee_invoke_func dispatches
 * into these veneers. WT_NSC_VENEER-based, FF-M-native veneers replace
 * this once built (item 3c-followup). Caller identity and CMSE/window
 * validation follow the same pattern as src/services/vnet/vnet_service.c. */
static int wt_ffm_veneer_caller(psa_client_id_t* caller)
{
    uint32_t guest_id = wt_platform_active_guest_id();

    if (guest_id >= (uint32_t)WT_MAX_GUESTS) {
        return 0;
    }
    *caller = -(psa_client_id_t)(guest_id + 1U);
    return 1;
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int32_t WolfTrust_FFM_Connect(uint32_t sid, uint32_t version)
{
    psa_client_id_t caller;

    if (!wt_ffm_veneer_caller(&caller)) {
        return (int32_t)PSA_NULL_HANDLE;
    }
    return (int32_t)wt_ffm_connect(&g_ffm_runtime, caller, sid, version);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int32_t WolfTrust_FFM_Call(int32_t handle, int32_t type,
                          const wt_ffm_veneer_iovec_t* ns_iovec)
{
    psa_client_id_t caller;
    wt_ffm_veneer_iovec_t iovec;
    psa_invec in_vec;
    psa_outvec out_vec;

    if (!wt_ffm_veneer_caller(&caller)) {
        return (int32_t)PSA_ERROR_PROGRAMMER_ERROR;
    }
    if (ns_iovec == NULL ||
            !wt_cmse_check_ns_ro(ns_iovec, sizeof(*ns_iovec))) {
        return (int32_t)PSA_ERROR_PROGRAMMER_ERROR;
    }
    /* Single read into a local copy: the struct's own fields are not
     * re-read after this, so a racing NS write cannot change the vector
     * base/length wt_ffm_call validates and copies from. */
    iovec = *ns_iovec;
    in_vec.base = iovec.input;
    in_vec.len = iovec.input_len;
    out_vec.base = iovec.output;
    out_vec.len = iovec.output_len;
    return (int32_t)wt_ffm_call(&g_ffm_runtime, caller, (psa_handle_t)handle,
                                type, &in_vec,
                                iovec.input_len != 0U ? 1U : 0U,
                                &out_vec,
                                iovec.output_len != 0U ? 1U : 0U);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
void WolfTrust_FFM_Close(int32_t handle)
{
    psa_client_id_t caller;

    if (!wt_ffm_veneer_caller(&caller)) {
        return;
    }
    (void)wt_ffm_close(&g_ffm_runtime, caller, (psa_handle_t)handle);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
uint32_t WolfTrust_FFM_FrameworkVersion(void)
{
    return wt_ffm_framework_version(&g_ffm_runtime);
}

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
uint32_t WolfTrust_FFM_ServiceVersion(uint32_t sid)
{
    psa_client_id_t caller;

    if (!wt_ffm_veneer_caller(&caller)) {
        return PSA_VERSION_NONE;
    }
    return wt_ffm_service_version(&g_ffm_runtime, caller, sid);
}

const wt_ffm_runtime_t* wt_ffm_boot_runtime(void)
{
    return &g_ffm_runtime;
}
