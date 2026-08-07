/* ffm_api.c
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

#include "wolftrust/ffm_api.h"

static wt_ffm_runtime_t* g_runtime;
static const wt_ffm_identity_ops_t* g_identity_ops;
static void* g_identity_context;

static psa_client_id_t wt_ffm_current_client(void)
{
    if (g_identity_ops == NULL || g_identity_ops->current_client == NULL)
        return 0;
    return g_identity_ops->current_client(g_identity_context);
}

static int32_t wt_ffm_current_partition(void)
{
    if (g_identity_ops == NULL || g_identity_ops->current_partition == NULL)
        return 0;
    return g_identity_ops->current_partition(g_identity_context);
}

static void wt_ffm_api_panic(void)
{
    int32_t partition_id = wt_ffm_current_partition();

    if (g_runtime != NULL && g_runtime->ops != NULL &&
            g_runtime->ops->panic != NULL) {
        g_runtime->ops->panic(g_runtime->port_context, partition_id);
    }
}

int wt_ffm_api_bind(wt_ffm_runtime_t* runtime,
                    const wt_ffm_identity_ops_t* identity_ops,
                    void* identity_context)
{
    if (runtime == NULL || runtime->manifest == NULL ||
            identity_ops == NULL || identity_ops->current_client == NULL ||
            identity_ops->current_partition == NULL) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    g_runtime = runtime;
    g_identity_ops = identity_ops;
    g_identity_context = identity_context;
    return WT_FFM_SUCCESS;
}

void wt_ffm_api_unbind(void)
{
    g_runtime = NULL;
    g_identity_ops = NULL;
    g_identity_context = NULL;
}

uint32_t psa_framework_version(void)
{
    return wt_ffm_framework_version(g_runtime);
}

uint32_t psa_version(uint32_t sid)
{
    return wt_ffm_service_version(g_runtime, wt_ffm_current_client(), sid);
}

psa_handle_t psa_connect(uint32_t sid, uint32_t version)
{
    return wt_ffm_connect(g_runtime, wt_ffm_current_client(), sid, version);
}

psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec* in_vec, size_t in_len,
                      psa_outvec* out_vec, size_t out_len)
{
    return wt_ffm_call(g_runtime, wt_ffm_current_client(), handle, type,
                       in_vec, in_len, out_vec, out_len);
}

void psa_close(psa_handle_t handle)
{
    if (wt_ffm_close(g_runtime, wt_ffm_current_client(), handle) !=
            WT_FFM_SUCCESS) {
        wt_ffm_api_panic();
    }
}

psa_signal_t psa_wait(psa_signal_t signal_mask, uint32_t timeout)
{
    psa_signal_t asserted = 0U;
    int ret;

    (void)timeout;
    ret = wt_ffm_wait(g_runtime, wt_ffm_current_partition(), signal_mask,
                      &asserted);
    return ret == WT_FFM_SUCCESS ? asserted : 0U;
}

psa_status_t psa_get(psa_signal_t signal, psa_msg_t* msg)
{
    return wt_ffm_get(g_runtime, wt_ffm_current_partition(), signal, msg);
}

void psa_set_rhandle(psa_handle_t msg_handle, void* rhandle)
{
    if (wt_ffm_set_rhandle(g_runtime, wt_ffm_current_partition(), msg_handle,
                           rhandle) != WT_FFM_SUCCESS) {
        wt_ffm_api_panic();
    }
}

size_t psa_read(psa_handle_t msg_handle, uint32_t invec_idx,
                void* buffer, size_t num_bytes)
{
    return wt_ffm_read(g_runtime, wt_ffm_current_partition(), msg_handle,
                       invec_idx, buffer, num_bytes);
}

size_t psa_skip(psa_handle_t msg_handle, uint32_t invec_idx,
                size_t num_bytes)
{
    return wt_ffm_skip(g_runtime, wt_ffm_current_partition(), msg_handle,
                       invec_idx, num_bytes);
}

void psa_write(psa_handle_t msg_handle, uint32_t outvec_idx,
               const void* buffer, size_t num_bytes)
{
    if (wt_ffm_write(g_runtime, wt_ffm_current_partition(), msg_handle,
                     outvec_idx, buffer, num_bytes) != WT_FFM_SUCCESS) {
        wt_ffm_api_panic();
    }
}

void psa_reply(psa_handle_t msg_handle, psa_status_t status)
{
    if (wt_ffm_reply(g_runtime, wt_ffm_current_partition(), msg_handle,
                     status) != WT_FFM_SUCCESS) {
        wt_ffm_api_panic();
    }
}

void psa_notify(int32_t partition_id)
{
    if (wt_ffm_notify(g_runtime, partition_id) != WT_FFM_SUCCESS)
        wt_ffm_api_panic();
}

void psa_clear(void)
{
    if (wt_ffm_clear(g_runtime, wt_ffm_current_partition()) !=
            WT_FFM_SUCCESS) {
        wt_ffm_api_panic();
    }
}

void psa_eoi(psa_signal_t irq_signal)
{
    (void)irq_signal;
    wt_ffm_api_panic();
}

void psa_panic(void)
{
    wt_ffm_api_panic();
    for (;;) {
    }
}
