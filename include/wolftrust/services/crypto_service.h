/* crypto_service.h
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

#ifndef WOLFTRUST_SERVICES_CRYPTO_SERVICE_H
#define WOLFTRUST_SERVICES_CRYPTO_SERVICE_H

#include "wolftrust/ffm.h"
#include "wolftrust/spm_gate.h"

/* Copied-IOVEC bound (WT-FFM-0041): the SPM copies request input into a
 * bounded private buffer before invoking the isolated compute, so the Secure
 * Partition reads only its own stack. Requests larger than this are refused;
 * kept well under the 8 KiB per-SP secure stack. */
#define WT_CRYPTO_SP_INPUT_MAX 2048U

/* Isolated Secure Partition compute (WT-FFM-0011): SHA-256 over a private
 * input buffer into a private digest buffer. Makes no psa_* calls -- the SPM
 * performs every IOVEC transfer -- so it runs entirely on the Secure
 * Partition's own secure stack with the MPU narrowed to that domain. */
int wt_crypto_sp_hash(const uint8_t* input, size_t input_len,
                      uint8_t* digest, size_t digest_len);

/* Compute seam for the copied-IOVEC hash. Host builds keep the default
 * (wt_crypto_sp_hash inline); the production port installs an isolated runner
 * that executes on the crypto SP's own secure stack under a narrowed MPU. */
typedef int (*wt_crypto_sp_compute_fn)(const uint8_t* input, size_t input_len,
                                       uint8_t* digest, size_t digest_len);
void wt_crypto_service_set_compute(wt_crypto_sp_compute_fn fn);

/* Transport seam for the loop's SPM requests. Default is the direct gate
 * call (host tests, privileged inline dispatch); the ARMv8-M port installs
 * an SVC transport when the loop runs as an unprivileged scheduled SP.
 * NULL restores the direct default. */
void wt_crypto_service_set_transport(wt_spm_transport_fn fn);

/* Key-op request types on the SERVICE_CRYPTO face (WT-FFM-0046). Type 0
 * (PSA_IPC_CALL) stays the SHA-256 hash; key ops forward to SERVICE_VAULT
 * over SP-to-SP IPC with the end client as delegated sub_owner — private
 * key material never enters this partition. */
#define WT_CRYPTO_OP_KEY_GENERATE      1
#define WT_CRYPTO_OP_KEY_IMPORT        2
#define WT_CRYPTO_OP_KEY_EXPORT_PUBLIC 3
#define WT_CRYPTO_OP_KEY_SIGN          4
#define WT_CRYPTO_OP_KEY_VERIFY        5
#define WT_CRYPTO_OP_KEY_ENCRYPT       6
#define WT_CRYPTO_OP_KEY_DECRYPT       7
#define WT_CRYPTO_OP_KEY_DESTROY       8

/* Client wire header: one concatenated input vector carries the header
 * followed directly by the op payload ([wt_crypto_key_req_t][payload]), so
 * the single-invec TEE transport reaches every op. usage/key_type use the
 * WT_VAULT_KEY_* encodings and matter only on generate/import. */
typedef struct wt_crypto_key_req {
    uint64_t uid;
    uint32_t usage;
    uint32_t key_type;
} wt_crypto_key_req_t;

/* Per-call transport + compute, passed as the dispatch context so an
 * unprivileged scheduled Secure Partition supplies them from its own stack
 * instead of reading the file-scope globals, which live in SPM RAM outside
 * the partition's MPU domain. A NULL dispatch context falls back to the
 * globals (host tests, privileged inline dispatch). vault_sid/vault_handle
 * carry the lazy SP-to-SP vault connection for key ops; key ops fail closed
 * (NOT_SUPPORTED) when the context carries no vault SID. */
typedef struct wt_crypto_service_ctx {
    wt_spm_transport_fn transport;
    wt_crypto_sp_compute_fn compute;
    uint32_t vault_sid;
    psa_handle_t vault_handle;
} wt_crypto_service_ctx_t;

/* SERVICE_CRYPTO's dispatch loop: wait, get, service one message, reply.
 * Architecture-neutral (no Armv8-M/CMSE dependency) so it is host-testable
 * through a real wt_ffm_connect/wt_ffm_call round trip. Supports a single
 * request today: PSA_IPC_CALL computes a SHA-256 digest of the input
 * vector into the output vector. */
int wt_crypto_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                               int32_t partition_id);

#endif /* WOLFTRUST_SERVICES_CRYPTO_SERVICE_H */
