/* psa_fwu_client.c
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

/* PSA Firmware Update API 1.0 client (SRC-PSA-FWU): marshals the public
 * psa_fwu_* calls onto the SERVICE_FWU wire protocol over the OS-neutral
 * FF-M client (WT-FFM-0053). Each operation runs one connect/call/close
 * round trip, so the client carries no connection state of its own. */

#include "psa/update.h"
#include "psa/client.h"
#include "psa_manifest/sid.h"

#include "wolftrust/services/fwu_service.h"

#include <string.h>

static psa_status_t wt_fwu_client_call(int32_t op, const wt_fwu_req_t* req,
                                       const void* block, size_t block_size,
                                       psa_fwu_component_info_t* info)
{
    psa_invec in_vec[1];
    psa_outvec out_vec[1];
    uint8_t buffer[sizeof(wt_fwu_req_t) + PSA_FWU_MAX_WRITE_SIZE];
    psa_handle_t handle;
    psa_status_t status;
    size_t in_len;

    if (block_size > PSA_FWU_MAX_WRITE_SIZE) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    (void)memcpy(buffer, req, sizeof(*req));
    in_len = sizeof(*req);
    if (block != NULL && block_size != 0u) {
        (void)memcpy(buffer + in_len, block, block_size);
        in_len += block_size;
    }
    handle = psa_connect(SERVICE_FWU_SID, 1u);
    if (handle <= 0) {
        return PSA_ERROR_COMMUNICATION_FAILURE;
    }
    in_vec[0].base = buffer;
    in_vec[0].len = in_len;
    out_vec[0].base = info;
    out_vec[0].len = (info != NULL) ? sizeof(*info) : 0u;
    status = psa_call(handle, op, in_vec, 1u, out_vec,
                      (info != NULL) ? 1u : 0u);
    psa_close(handle);
    return status;
}

psa_status_t psa_fwu_query(psa_fwu_component_t component,
                           psa_fwu_component_info_t* info)
{
    wt_fwu_req_t req;

    if (info == NULL) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    (void)memset(&req, 0, sizeof(req));
    req.component = component;
    return wt_fwu_client_call(WT_FWU_OP_QUERY, &req, NULL, 0u, info);
}

psa_status_t psa_fwu_start(psa_fwu_component_t component,
                           const void* manifest, size_t manifest_size)
{
    wt_fwu_req_t req;
    uint32_t version;

    /* PSA FWU 1.0: the detached manifest is optional. (NULL, 0) means the
     * metadata rides in the image header, so the anti-rollback version is
     * bound from that header at finish; a detached manifest is wolfTrust's
     * monotonic version word, which fails a rolled-back candidate before any
     * flash is touched. */
    if (manifest == NULL) {
        if (manifest_size != 0u) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        version = WT_FWU_VERSION_UNDECLARED;
    }
    else {
        if (manifest_size != sizeof(version)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        (void)memcpy(&version, manifest, sizeof(version));
        if (version == WT_FWU_VERSION_UNDECLARED) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
    }
    (void)memset(&req, 0, sizeof(req));
    req.component = component;
    req.version = version;
    return wt_fwu_client_call(WT_FWU_OP_START, &req, NULL, 0u, NULL);
}

psa_status_t psa_fwu_write(psa_fwu_component_t component, size_t image_offset,
                           const void* block, size_t block_size)
{
    wt_fwu_req_t req;

    if (block == NULL || block_size == 0u) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    (void)memset(&req, 0, sizeof(req));
    req.component = component;
    req.offset = (uint32_t)image_offset;
    req.size = (uint32_t)block_size;
    return wt_fwu_client_call(WT_FWU_OP_WRITE, &req, block, block_size, NULL);
}

psa_status_t psa_fwu_finish(psa_fwu_component_t component)
{
    wt_fwu_req_t req;

    (void)memset(&req, 0, sizeof(req));
    req.component = component;
    return wt_fwu_client_call(WT_FWU_OP_FINISH, &req, NULL, 0u, NULL);
}

psa_status_t psa_fwu_cancel(psa_fwu_component_t component)
{
    wt_fwu_req_t req;

    (void)memset(&req, 0, sizeof(req));
    req.component = component;
    return wt_fwu_client_call(WT_FWU_OP_CANCEL, &req, NULL, 0u, NULL);
}

psa_status_t psa_fwu_clean(psa_fwu_component_t component)
{
    wt_fwu_req_t req;

    (void)memset(&req, 0, sizeof(req));
    req.component = component;
    return wt_fwu_client_call(WT_FWU_OP_CLEAN, &req, NULL, 0u, NULL);
}

psa_status_t psa_fwu_install(void)
{
    wt_fwu_req_t req;

    (void)memset(&req, 0, sizeof(req));
    return wt_fwu_client_call(WT_FWU_OP_INSTALL, &req, NULL, 0u, NULL);
}

psa_status_t psa_fwu_request_reboot(void)
{
    wt_fwu_req_t req;

    (void)memset(&req, 0, sizeof(req));
    return wt_fwu_client_call(WT_FWU_OP_REBOOT, &req, NULL, 0u, NULL);
}

psa_status_t psa_fwu_reject(psa_status_t error)
{
    wt_fwu_req_t req;

    (void)memset(&req, 0, sizeof(req));
    /* The error detail rides the version field of the wire header. */
    req.version = (uint32_t)error;
    return wt_fwu_client_call(WT_FWU_OP_REJECT, &req, NULL, 0u, NULL);
}

psa_status_t psa_fwu_accept(void)
{
    wt_fwu_req_t req;

    (void)memset(&req, 0, sizeof(req));
    return wt_fwu_client_call(WT_FWU_OP_ACCEPT, &req, NULL, 0u, NULL);
}
