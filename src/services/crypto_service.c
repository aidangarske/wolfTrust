/* crypto_service.c
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

#include "wolftrust/services/crypto_service.h"
#include "wolftrust/services/vault_service.h"
#include "wolftrust/spm_gate.h"

#include <string.h>

#include <wolfssl/wolfcrypt/sha256.h>

int wt_crypto_sp_hash(const uint8_t* input, size_t input_len,
                      uint8_t* digest, size_t digest_len)
{
    wc_Sha256 sha;
    int ret;

    if ((input == NULL && input_len != 0U) || digest == NULL ||
            digest_len < WC_SHA256_DIGEST_SIZE) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    /* INVALID_DEVID keeps the isolated SP compute pure software: with
     * WOLF_CRYPTO_CB the default devId routes SHA through the shared crypto
     * callback registry (wolfHSM), which lies outside the SP's MPU domain. */
    ret = wc_InitSha256_ex(&sha, NULL, INVALID_DEVID);
    if (ret == 0) {
        ret = wc_Sha256Update(&sha, input, (word32)input_len);
    }
    if (ret == 0) {
        ret = wc_Sha256Final(&sha, digest);
    }
    if (ret != 0) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

/* Compute seam: the host default runs wt_crypto_sp_hash inline; the production
 * port installs an isolated runner (own SP stack + narrowed MPU) at boot. */
static wt_crypto_sp_compute_fn g_crypto_sp_compute = wt_crypto_sp_hash;

void wt_crypto_service_set_compute(wt_crypto_sp_compute_fn fn)
{
    g_crypto_sp_compute = (fn != NULL) ? fn : wt_crypto_sp_hash;
}

static wt_spm_transport_fn g_spm_transport = wt_spm_transport_direct;

void wt_crypto_service_set_transport(wt_spm_transport_fn fn)
{
    g_spm_transport = (fn != NULL) ? fn : wt_spm_transport_direct;
}

/* Resolve transport/compute from the dispatch context. The early return on a
 * non-NULL context keeps the fallback global load out of the scheduled-SP
 * path entirely, so the unprivileged partition never touches SPM RAM. */
static wt_spm_transport_fn wt_crypto_resolve_transport(
    const wt_crypto_service_ctx_t* ctx)
{
    if (ctx != NULL) {
        return ctx->transport;
    }
    return g_spm_transport;
}

static wt_crypto_sp_compute_fn wt_crypto_resolve_compute(
    const wt_crypto_service_ctx_t* ctx)
{
    if (ctx != NULL) {
        return ctx->compute;
    }
    return g_crypto_sp_compute;
}

static int wt_crypto_service_hash(wt_ffm_runtime_t* runtime,
                                  int32_t partition_id,
                                  const psa_msg_t* msg,
                                  wt_spm_transport_fn transport,
                                  wt_crypto_sp_compute_fn compute)
{
    uint8_t input[WT_CRYPTO_SP_INPUT_MAX];
    uint8_t digest[WC_SHA256_DIGEST_SIZE];
    wt_spm_call_t call;
    size_t in_len = 0U;
    int ret;

    /* Copied IOVEC (WT-FFM-0041): the SPM drains the input vector into a
     * bounded private buffer, refusing anything past the SP input cap, so the
     * isolated compute never reads caller memory. */
    if (msg->in_size[0] > sizeof(input)) {
        return WT_FFM_ERROR_ARGUMENT;
    }
    for (;;) {
        (void)memset(&call, 0, sizeof(call));
        call.op = WT_SPM_OP_READ;
        call.partition_id = partition_id;
        call.msg_handle = msg->handle;
        call.buffer = input + in_len;
        call.num_bytes = sizeof(input) - in_len;
        if (transport(runtime, &call) != WT_FFM_SUCCESS) {
            return WT_FFM_ERROR_STATE;
        }
        if (call.ret_size == 0U) {
            break;
        }
        in_len += call.ret_size;
    }

    ret = compute(input, in_len, digest, sizeof(digest));
    if (ret != WT_FFM_SUCCESS) {
        return ret;
    }
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WRITE;
    call.partition_id = partition_id;
    call.msg_handle = msg->handle;
    call.buffer = digest;
    call.num_bytes = sizeof(digest);
    if (transport(runtime, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

/* One completed gate op: both transports finish blocking SP-as-client ops
 * before returning; a residual NOT_READY is a failure, never a spin. */
static int wt_crypto_xfer(wt_spm_transport_fn transport,
                          wt_ffm_runtime_t* runtime, wt_spm_call_t* call)
{
    if (transport(runtime, call) != WT_FFM_SUCCESS ||
            call->ret_int == WT_FFM_ERROR_NOT_READY) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

/* Lazy SP-to-SP connection to the vault, cached across messages. */
static psa_status_t wt_crypto_vault_handle(wt_crypto_service_ctx_t* ctx,
                                           wt_ffm_runtime_t* runtime,
                                           int32_t partition_id)
{
    wt_spm_call_t call;

    if (ctx->vault_handle > 0) {
        return PSA_SUCCESS;
    }
    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_CONNECT;
    call.partition_id = partition_id;
    call.sid = ctx->vault_sid;
    call.version = 1U;
    if (wt_crypto_xfer(ctx->transport, runtime, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS || call.ret_handle <= 0) {
        return PSA_ERROR_GENERIC_ERROR;
    }
    ctx->vault_handle = call.ret_handle;
    return PSA_SUCCESS;
}

static psa_status_t wt_crypto_vault_call(wt_crypto_service_ctx_t* ctx,
                                         wt_ffm_runtime_t* runtime,
                                         int32_t partition_id, int32_t type,
                                         const wt_vault_req_t* vreq,
                                         const uint8_t* in_data,
                                         size_t in_len, void* out,
                                         size_t out_cap, size_t* out_len)
{
    wt_spm_call_t call;

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_CALL;
    call.partition_id = partition_id;
    call.msg_handle = ctx->vault_handle;
    call.call_type = type;
    call.sp_in[0].base = vreq;
    call.sp_in[0].len = sizeof(*vreq);
    call.sp_in_len = 1U;
    if (in_data != NULL) {
        call.sp_in[1].base = in_data;
        call.sp_in[1].len = in_len;
        call.sp_in_len = 2U;
    }
    if (out != NULL) {
        call.sp_out[0].base = out;
        call.sp_out[0].len = out_cap;
        call.sp_out_len = 1U;
    }
    if (wt_crypto_xfer(ctx->transport, runtime, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return PSA_ERROR_GENERIC_ERROR;
    }
    if (out_len != NULL) {
        *out_len = call.sp_out[0].len;
    }
    return call.ret_status;
}

/* Drain invec[0] — [wt_crypto_key_req_t][payload] — into a bounded private
 * buffer (WT-FFM-0041 copied transfers). */
static int wt_crypto_read_req(wt_spm_transport_fn transport,
                              wt_ffm_runtime_t* runtime, int32_t partition_id,
                              psa_handle_t msg_handle, uint8_t* buffer,
                              size_t capacity, size_t* out_len)
{
    wt_spm_call_t call;
    size_t len = 0U;

    for (;;) {
        (void)memset(&call, 0, sizeof(call));
        call.op = WT_SPM_OP_READ;
        call.partition_id = partition_id;
        call.msg_handle = msg_handle;
        call.vec_idx = 0U;
        call.buffer = buffer + len;
        call.num_bytes = capacity - len;
        if (transport(runtime, &call) != WT_FFM_SUCCESS) {
            return WT_FFM_ERROR_STATE;
        }
        if (call.ret_size == 0U) {
            break;
        }
        len += call.ret_size;
        if (len >= capacity) {
            break;
        }
    }
    *out_len = len;
    return WT_FFM_SUCCESS;
}

static int wt_crypto_write_reply(wt_spm_transport_fn transport,
                                 wt_ffm_runtime_t* runtime,
                                 int32_t partition_id,
                                 psa_handle_t msg_handle, const void* data,
                                 size_t len)
{
    wt_spm_call_t call;

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WRITE;
    call.partition_id = partition_id;
    call.msg_handle = msg_handle;
    call.vec_idx = 0U;
    call.buffer = (void*)(uintptr_t)data;
    call.num_bytes = len;
    if (transport(runtime, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}

/* Key ops (WT-FFM-0046): parse the client header, then forward to the vault
 * with the SPM-stamped end client as delegated sub_owner. This partition
 * only marshals — private key material never enters its domain. */
static psa_status_t wt_crypto_service_keys(wt_crypto_service_ctx_t* ctx,
                                           wt_ffm_runtime_t* runtime,
                                           int32_t partition_id,
                                           const psa_msg_t* msg)
{
    uint8_t buffer[sizeof(wt_crypto_key_req_t) + WT_VAULT_OBJECT_MAX];
    uint8_t out[WT_VAULT_OBJECT_MAX];
    wt_crypto_key_req_t req;
    wt_vault_req_t vreq;
    size_t in_len = 0U;
    size_t out_len = 0U;
    size_t cap;
    int32_t vop;
    int has_out = 0;
    psa_status_t status;

    if (msg->in_size[0] < sizeof(req) || msg->in_size[0] > sizeof(buffer)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (wt_crypto_read_req(ctx->transport, runtime, partition_id,
                           msg->handle, buffer, sizeof(buffer),
                           &in_len) != WT_FFM_SUCCESS ||
            in_len < sizeof(req)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    (void)memcpy(&req, buffer, sizeof(req));

    status = wt_crypto_vault_handle(ctx, runtime, partition_id);
    if (status != PSA_SUCCESS) {
        return status;
    }

    (void)memset(&vreq, 0, sizeof(vreq));
    vreq.uid = req.uid;
    vreq.flags = req.usage;
    vreq.reserved = req.key_type;
    vreq.sub_owner = msg->client_id;

    switch (msg->type) {
    case WT_CRYPTO_OP_KEY_GENERATE:
        vop = WT_VAULT_OP_KEY_GENERATE;
        break;
    case WT_CRYPTO_OP_KEY_IMPORT:
        vop = WT_VAULT_OP_KEY_IMPORT;
        break;
    case WT_CRYPTO_OP_KEY_EXPORT_PUBLIC:
        vop = WT_VAULT_OP_KEY_EXPORT_PUBLIC;
        has_out = 1;
        break;
    case WT_CRYPTO_OP_KEY_SIGN:
        vop = WT_VAULT_OP_KEY_SIGN;
        has_out = 1;
        break;
    case WT_CRYPTO_OP_KEY_VERIFY:
        vop = WT_VAULT_OP_KEY_VERIFY;
        break;
    case WT_CRYPTO_OP_KEY_ENCRYPT:
        vop = WT_VAULT_OP_KEY_ENCRYPT;
        has_out = 1;
        break;
    case WT_CRYPTO_OP_KEY_DECRYPT:
        vop = WT_VAULT_OP_KEY_DECRYPT;
        has_out = 1;
        break;
    case WT_CRYPTO_OP_KEY_DESTROY:
        vop = WT_VAULT_OP_REMOVE;
        break;
    default:
        return PSA_ERROR_NOT_SUPPORTED;
    }

    cap = msg->out_size[0];
    if (cap > sizeof(out)) {
        cap = sizeof(out);
    }
    status = wt_crypto_vault_call(ctx, runtime, partition_id, vop, &vreq,
                                  (in_len > sizeof(req)) ?
                                      buffer + sizeof(req) : NULL,
                                  in_len - sizeof(req),
                                  (has_out != 0) ? out : NULL, cap,
                                  (has_out != 0) ? &out_len : NULL);
    if (status == PSA_SUCCESS && has_out != 0 &&
            wt_crypto_write_reply(ctx->transport, runtime, partition_id,
                                  msg->handle, out, out_len) !=
                WT_FFM_SUCCESS) {
        status = PSA_ERROR_GENERIC_ERROR;
    }
    return status;
}

/* Vault-backed randomness (WT-FFM-0054): forward to the vault's RNG over the
 * cached SP-to-SP connection and reply with exactly out_size[0] bytes. This
 * partition only marshals — entropy never originates in its domain. */
static psa_status_t wt_crypto_service_random(wt_crypto_service_ctx_t* ctx,
                                             wt_ffm_runtime_t* runtime,
                                             int32_t partition_id,
                                             const psa_msg_t* msg)
{
    uint8_t out[WT_CRYPTO_RANDOM_MAX];
    wt_vault_req_t vreq;
    size_t out_len = 0U;
    size_t cap;
    psa_status_t status;

    cap = msg->out_size[0];
    if (cap == 0U || cap > sizeof(out)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    status = wt_crypto_vault_handle(ctx, runtime, partition_id);
    if (status != PSA_SUCCESS) {
        return status;
    }
    (void)memset(&vreq, 0, sizeof(vreq));
    vreq.sub_owner = msg->client_id;
    status = wt_crypto_vault_call(ctx, runtime, partition_id,
                                  WT_VAULT_OP_RANDOM, &vreq, NULL, 0U, out,
                                  cap, &out_len);
    if (status == PSA_SUCCESS) {
        if (out_len != cap ||
                wt_crypto_write_reply(ctx->transport, runtime, partition_id,
                                      msg->handle, out, out_len) !=
                    WT_FFM_SUCCESS) {
            status = PSA_ERROR_GENERIC_ERROR;
        }
    }
    return status;
}

int wt_crypto_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                               int32_t partition_id)
{
    wt_crypto_service_ctx_t* ctx = (wt_crypto_service_ctx_t*)context;
    wt_spm_transport_fn transport = wt_crypto_resolve_transport(ctx);
    wt_crypto_sp_compute_fn compute = wt_crypto_resolve_compute(ctx);
    psa_signal_t asserted = 0U;
    psa_msg_t msg;
    psa_status_t reply_status;
    wt_spm_call_t call;

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_WAIT;
    call.partition_id = partition_id;
    call.signal_mask = PSA_WAIT_ANY;
    call.timeout = PSA_BLOCK;
    call.asserted = &asserted;
    if (transport(runtime, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_GET;
    call.partition_id = partition_id;
    call.signal = asserted;
    call.msg = &msg;
    if (transport(runtime, &call) != WT_FFM_SUCCESS ||
            call.ret_status != PSA_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }

    if (msg.type == PSA_IPC_CONNECT || msg.type == PSA_IPC_DISCONNECT) {
        reply_status = PSA_SUCCESS;
    } else if (msg.type == PSA_IPC_CALL) {
        reply_status = wt_crypto_service_hash(runtime, partition_id, &msg,
                           transport, compute) == WT_FFM_SUCCESS ?
                       PSA_SUCCESS : PSA_ERROR_GENERIC_ERROR;
    } else if (msg.type >= WT_CRYPTO_OP_KEY_GENERATE &&
               msg.type <= WT_CRYPTO_OP_KEY_DESTROY) {
        /* Key ops need the vault route from the dispatch context; without
         * one they fail closed — no in-partition key fallback exists. */
        if (ctx == NULL || ctx->vault_sid == 0U) {
            reply_status = PSA_ERROR_NOT_SUPPORTED;
        } else {
            reply_status = wt_crypto_service_keys(ctx, runtime, partition_id,
                                                  &msg);
        }
    } else if (msg.type == WT_CRYPTO_OP_RANDOM) {
        /* Randomness rides the same vault route and fails closed without
         * one — this partition holds no entropy source of its own. */
        if (ctx == NULL || ctx->vault_sid == 0U) {
            reply_status = PSA_ERROR_NOT_SUPPORTED;
        } else {
            reply_status = wt_crypto_service_random(ctx, runtime,
                                                    partition_id, &msg);
        }
    } else {
        reply_status = PSA_ERROR_NOT_SUPPORTED;
    }

    (void)memset(&call, 0, sizeof(call));
    call.op = WT_SPM_OP_REPLY;
    call.partition_id = partition_id;
    call.msg_handle = msg.handle;
    call.status = reply_status;
    if (transport(runtime, &call) != WT_FFM_SUCCESS ||
            call.ret_int != WT_FFM_SUCCESS) {
        return WT_FFM_ERROR_STATE;
    }
    return WT_FFM_SUCCESS;
}
