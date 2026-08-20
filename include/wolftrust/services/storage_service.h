/* storage_service.h
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

#ifndef WOLFTRUST_SERVICES_STORAGE_SERVICE_H
#define WOLFTRUST_SERVICES_STORAGE_SERVICE_H

#include "wolftrust/ffm.h"
#include "wolftrust/spm_gate.h"
#include "wolftrust/services/vault_service.h"

/* SERVICE_ITS: PSA Internal Trusted Storage as an UNPRIVILEGED isolated
 * Secure Partition (SRC-PSA-STORAGE). The partition holds no storage of its
 * own — every object round-trips to SERVICE_VAULT over SP-to-SP FF-M IPC
 * through the SVC gate, authorized by the manifest dependencies[] entry
 * (WT-FFM-0047). Each end client's objects are namespaced by the vault under
 * (ITS partition, client id, uid) via the delegated sub_owner (WT-FFM-0044). */

/* psa_call request types (client face). */
#define WT_ITS_OP_SET      1
#define WT_ITS_OP_GET      2
#define WT_ITS_OP_GET_INFO 3
#define WT_ITS_OP_REMOVE   4

/* Client wire header. One concatenated input vector carries the header
 * followed directly by the object data on SET ([wt_its_req_t][data]), so a
 * single-invec client transport reaches every op; GET/GET_INFO/REMOVE send
 * the header alone and GET/GET_INFO read the reply from outvec[0]. */
typedef struct wt_its_req {
    uint64_t uid;
    uint32_t flags;    /* SET: PSA create flags (WT_VAULT_FLAG_*) */
    uint32_t offset;   /* GET: read offset into the object */
} wt_its_req_t;

/* Per-loop dispatch context, built on the Secure Partition's own stack (the
 * unprivileged loop cannot read file-scope globals in SPM RAM). vault_handle
 * caches the SP-to-SP connection across messages; 0 means not yet connected. */
typedef struct wt_storage_service_ctx {
    wt_spm_transport_fn transport;
    uint32_t vault_sid;
    psa_handle_t vault_handle;
} wt_storage_service_ctx_t;

/* SERVICE_ITS's dispatch loop: wait, get, service one message via the vault,
 * reply. Architecture-neutral: the host test drives the full client → ITS →
 * vault → NVM chain through real wt_ffm round trips; the target runs the same
 * code as a scheduled unprivileged coroutine. A NULL context fails closed. */
int wt_storage_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                                int32_t partition_id);

#endif /* WOLFTRUST_SERVICES_STORAGE_SERVICE_H */
