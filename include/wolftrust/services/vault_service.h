/* vault_service.h
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

#ifndef WOLFTRUST_SERVICES_VAULT_SERVICE_H
#define WOLFTRUST_SERVICES_VAULT_SERVICE_H

#include "wolftrust/ffm.h"
#include "wolftrust/spm_gate.h"

/* SERVICE_VAULT: the gated wolfHSM backing (WT-FFM-0047). Secure Partitions
 * (ITS/PS/Crypto) reach persistent vault storage only through this FF-M
 * service; the owner of every object is the SPM-stamped caller identity,
 * never a caller-supplied field (WT-FFM-0044). Non-secure clients are
 * refused by the manifest (nonsecure_clients = false). */

/* psa_call request types. */
#define WT_VAULT_OP_SET      1
#define WT_VAULT_OP_GET      2
#define WT_VAULT_OP_GET_INFO 3
#define WT_VAULT_OP_REMOVE   4

/* PSA storage create flags understood by the vault (SRC-PSA-STORAGE). The
 * NO_* bits are client hints recorded for get_info fidelity; the vault always
 * stores at least as securely as requested. */
#define WT_VAULT_FLAG_WRITE_ONCE         0x1U
#define WT_VAULT_FLAG_NO_CONFIDENTIALITY 0x2U
#define WT_VAULT_FLAG_NO_REPLAY          0x4U

/* Internal flag a storage frontend ORs in (never a PSA create flag): the
 * object is AES-GCM sealed under the device-unique wolfHSM key with the
 * monotonic rollback counter as nonce (WT-FFM-0048). Sealing runs entirely
 * inside the privileged vault domain. */
#define WT_VAULT_FLAG_SEALED 0x10000U

/* Copied-IOVEC object bound (WT-FFM-0041): requests larger than this are
 * refused, kept well under the vault partition's 8 KiB secure stack. */
#define WT_VAULT_OBJECT_MAX 1024U

/* invec[0] of every vault request. uid first keeps the layout padding-free.
 * sub_owner is a DELEGATED namespace for storage-frontend partitions (ITS/PS)
 * forwarding on behalf of their own clients: the vault always namespaces
 * primarily by the SPM-stamped caller, so a caller can only ever partition
 * its OWN namespace further — sub_owner cannot reach another owner's data. */
typedef struct wt_vault_req {
    uint64_t uid;
    uint32_t flags;      /* SET: PSA create flags */
    uint32_t offset;     /* GET: read offset into the object */
    int32_t  sub_owner;  /* frontend-delegated end-client id (0 = none) */
    uint32_t reserved;
} wt_vault_req_t;

/* outvec[0] of GET_INFO. */
typedef struct wt_vault_info {
    uint32_t capacity;
    uint32_t size;
    uint32_t flags;    /* PSA create flags recorded at SET */
    uint32_t reserved;
} wt_vault_info_t;

/* Backing store vtable. Each op returns a psa_status_t; owner is the
 * SPM-stamped caller partition identity, sub the delegated end-client. */
typedef struct wt_vault_backend {
    psa_status_t (*set)(int32_t owner, int32_t sub, uint64_t uid,
                        uint32_t flags, const uint8_t* data, size_t len);
    psa_status_t (*get)(int32_t owner, int32_t sub, uint64_t uid,
                        uint32_t offset, uint8_t* data, size_t size,
                        size_t* out_len);
    psa_status_t (*get_info)(int32_t owner, int32_t sub, uint64_t uid,
                             wt_vault_info_t* info);
    psa_status_t (*remove)(int32_t owner, int32_t sub, uint64_t uid);
} wt_vault_backend_t;

/* Install the backing store. NULL restores the fail-closed default, which
 * refuses every request with PSA_ERROR_NOT_SUPPORTED. */
void wt_vault_service_set_backend(const wt_vault_backend_t* backend);

/* Transport seam, mirroring crypto_service: direct gate calls on the host,
 * the SVC transport when scheduled on target. NULL restores the default. */
void wt_vault_service_set_transport(wt_spm_transport_fn fn);

/* SERVICE_VAULT's dispatch loop: wait, get, service one message, reply.
 * Architecture-neutral so the same code is host-tested through a real
 * wt_ffm_connect/wt_ffm_call round trip and scheduled on target. */
int wt_vault_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                              int32_t partition_id);

#endif /* WOLFTRUST_SERVICES_VAULT_SERVICE_H */
