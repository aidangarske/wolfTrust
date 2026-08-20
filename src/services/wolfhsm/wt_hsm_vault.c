/* wt_hsm_vault.c
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

/* Gated wolfHSM vault backing (WT-FFM-0044/0045/0047): PSA storage objects
 * live as wolfHSM NVM objects keyed by the SPM-stamped owner identity plus
 * the caller's 64-bit uid, recorded in the object label. Only the *Checked
 * NVM entry points are used so the immutable-object flags are enforced by
 * the vault, not by caller convention. Ops run to completion on the single
 * cooperative core without yielding, matching the per-guest HSM server
 * tasklets' serialisation of the shared NVM context. */

#include "wolftrust/services/vault_service.h"
#include "wolftrust/services/hsm.h"

#include <string.h>

#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_common.h"
#include "wolfhsm/wh_nvm.h"

/* Vault NVM id window: plain-NVM id space (type nibble 0), disjoint from the
 * keystore's composed ids (type nibble >= 1, e.g. the attestation IAK). */
#define WT_HSM_VAULT_ID_BASE  0x0100U
#define WT_HSM_VAULT_ID_COUNT 32U

#define WT_HSM_VAULT_LABEL_MAGIC 0x31565457UL /* "WTV1" little-endian */

static whNvmContext* g_vault_nvm;

int wt_hsm_vault_init(whNvmContext* nvm)
{
    if (nvm == NULL) {
        return -1;
    }
    g_vault_nvm = nvm;
    return 0;
}

static void wt_hsm_vault_label(uint8_t* label, int32_t owner, int32_t sub,
                               uint64_t uid, uint32_t flags)
{
    uint32_t magic = WT_HSM_VAULT_LABEL_MAGIC;

    (void)memset(label, 0, WH_NVM_LABEL_LEN);
    (void)memcpy(label, &magic, sizeof(magic));
    (void)memcpy(label + 4, &owner, sizeof(owner));
    (void)memcpy(label + 8, &uid, sizeof(uid));
    (void)memcpy(label + 16, &flags, sizeof(flags));
    (void)memcpy(label + 20, &sub, sizeof(sub));
}

static int wt_hsm_vault_label_match(const uint8_t* label, int32_t owner,
                                    int32_t sub, uint64_t uid)
{
    uint32_t magic;
    int32_t l_owner;
    int32_t l_sub;
    uint64_t l_uid;

    (void)memcpy(&magic, label, sizeof(magic));
    (void)memcpy(&l_owner, label + 4, sizeof(l_owner));
    (void)memcpy(&l_uid, label + 8, sizeof(l_uid));
    (void)memcpy(&l_sub, label + 20, sizeof(l_sub));
    return magic == WT_HSM_VAULT_LABEL_MAGIC && l_owner == owner &&
           l_sub == sub && l_uid == uid;
}

static uint32_t wt_hsm_vault_label_flags(const uint8_t* label)
{
    uint32_t flags;

    (void)memcpy(&flags, label + 16, sizeof(flags));
    return flags;
}

/* Find the (owner, uid) object in the vault id window. Returns PSA_SUCCESS
 * with the id + metadata, or PSA_ERROR_DOES_NOT_EXIST. out_free_id receives
 * the lowest unused id in the window (WH_NVM_ID_INVALID when full). */
static psa_status_t wt_hsm_vault_find(int32_t owner, int32_t sub,
                                      uint64_t uid, whNvmId* out_id,
                                      whNvmMetadata* out_meta,
                                      whNvmId* out_free_id)
{
    whNvmMetadata meta;
    whNvmId id;
    whNvmId free_id = WH_NVM_ID_INVALID;
    uint32_t i;
    int rc;
    psa_status_t status = PSA_ERROR_DOES_NOT_EXIST;

    for (i = 0U; i < WT_HSM_VAULT_ID_COUNT; i++) {
        id = (whNvmId)(WT_HSM_VAULT_ID_BASE + i);
        rc = wh_Nvm_GetMetadata(g_vault_nvm, id, &meta);
        if (rc == WH_ERROR_NOTFOUND) {
            if (free_id == WH_NVM_ID_INVALID) {
                free_id = id;
            }
        }
        else if (rc == WH_ERROR_OK) {
            if (wt_hsm_vault_label_match(meta.label, owner, sub, uid)) {
                if (out_id != NULL) {
                    *out_id = id;
                }
                if (out_meta != NULL) {
                    *out_meta = meta;
                }
                status = PSA_SUCCESS;
                break;
            }
        }
        else {
            status = PSA_ERROR_STORAGE_FAILURE;
            break;
        }
    }
    if (out_free_id != NULL) {
        *out_free_id = free_id;
    }
    return status;
}

static psa_status_t wt_hsm_vault_map_err(int rc)
{
    psa_status_t status;

    switch (rc) {
    case WH_ERROR_OK:
        status = PSA_SUCCESS;
        break;
    case WH_ERROR_ACCESS:
        status = PSA_ERROR_NOT_PERMITTED;
        break;
    case WH_ERROR_NOSPACE:
        status = PSA_ERROR_INSUFFICIENT_STORAGE;
        break;
    case WH_ERROR_NOTFOUND:
        status = PSA_ERROR_DOES_NOT_EXIST;
        break;
    default:
        status = PSA_ERROR_STORAGE_FAILURE;
        break;
    }
    return status;
}

static psa_status_t wt_hsm_vault_set(int32_t owner, int32_t sub,
                                     uint64_t uid, uint32_t flags,
                                     const uint8_t* data, size_t len)
{
    whNvmMetadata meta;
    whNvmId id = WH_NVM_ID_INVALID;
    whNvmId free_id = WH_NVM_ID_INVALID;
    psa_status_t status;

    if (g_vault_nvm == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    if (len > WT_VAULT_OBJECT_MAX ||
            (flags & ~(uint32_t)WT_VAULT_FLAG_WRITE_ONCE) != 0U) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    status = wt_hsm_vault_find(owner, sub, uid, &id, &meta, &free_id);
    if (status == PSA_SUCCESS) {
        /* Existing object: honour WRITE_ONCE before any backend write; the
         * *Checked add enforces the same policy at the NVM layer. */
        if ((wt_hsm_vault_label_flags(meta.label) &
                WT_VAULT_FLAG_WRITE_ONCE) != 0U) {
            return PSA_ERROR_NOT_PERMITTED;
        }
    }
    else if (status == PSA_ERROR_DOES_NOT_EXIST) {
        if (free_id == WH_NVM_ID_INVALID) {
            return PSA_ERROR_INSUFFICIENT_STORAGE;
        }
        id = free_id;
    }
    else {
        return status;
    }

    (void)memset(&meta, 0, sizeof(meta));
    meta.id = id;
    meta.access = WH_NVM_ACCESS_ANY;
    meta.flags = WH_NVM_FLAGS_SENSITIVE;
    if ((flags & WT_VAULT_FLAG_WRITE_ONCE) != 0U) {
        meta.flags |= WH_NVM_FLAGS_NONMODIFIABLE |
                      WH_NVM_FLAGS_NONDESTROYABLE;
    }
    meta.len = (whNvmSize)len;
    wt_hsm_vault_label(meta.label, owner, sub, uid, flags);
    return wt_hsm_vault_map_err(
        wh_Nvm_AddObjectChecked(g_vault_nvm, &meta, (whNvmSize)len, data));
}

static psa_status_t wt_hsm_vault_get(int32_t owner, int32_t sub,
                                     uint64_t uid, uint32_t offset,
                                     uint8_t* data, size_t size,
                                     size_t* out_len)
{
    whNvmMetadata meta;
    whNvmId id = WH_NVM_ID_INVALID;
    size_t n;
    psa_status_t status;

    if (g_vault_nvm == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    status = wt_hsm_vault_find(owner, sub, uid, &id, &meta, NULL);
    if (status != PSA_SUCCESS) {
        return status;
    }
    if (offset > meta.len) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    n = (size_t)meta.len - offset;
    if (n > size) {
        n = size;
    }
    if (n > 0U) {
        status = wt_hsm_vault_map_err(
            wh_Nvm_ReadChecked(g_vault_nvm, id, (whNvmSize)offset,
                               (whNvmSize)n, data));
        if (status != PSA_SUCCESS) {
            return status;
        }
    }
    *out_len = n;
    return PSA_SUCCESS;
}

static psa_status_t wt_hsm_vault_get_info(int32_t owner, int32_t sub,
                                          uint64_t uid, wt_vault_info_t* info)
{
    whNvmMetadata meta;
    psa_status_t status;

    if (g_vault_nvm == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    status = wt_hsm_vault_find(owner, sub, uid, NULL, &meta, NULL);
    if (status != PSA_SUCCESS) {
        return status;
    }
    info->capacity = meta.len;
    info->size = meta.len;
    info->flags = wt_hsm_vault_label_flags(meta.label);
    info->reserved = 0U;
    return PSA_SUCCESS;
}

static psa_status_t wt_hsm_vault_remove(int32_t owner, int32_t sub,
                                        uint64_t uid)
{
    whNvmMetadata meta;
    whNvmId id = WH_NVM_ID_INVALID;
    psa_status_t status;

    if (g_vault_nvm == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    status = wt_hsm_vault_find(owner, sub, uid, &id, &meta, NULL);
    if (status != PSA_SUCCESS) {
        return status;
    }
    if ((wt_hsm_vault_label_flags(meta.label) &
            WT_VAULT_FLAG_WRITE_ONCE) != 0U) {
        return PSA_ERROR_NOT_PERMITTED;
    }
    return wt_hsm_vault_map_err(
        wh_Nvm_DestroyObjectsChecked(g_vault_nvm, 1U, &id));
}

const wt_vault_backend_t wt_hsm_vault_backend = {
    wt_hsm_vault_set,
    wt_hsm_vault_get,
    wt_hsm_vault_get_info,
    wt_hsm_vault_remove
};
