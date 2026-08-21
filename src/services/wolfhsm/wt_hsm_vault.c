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
#define WT_HSM_VAULT_TABLE_MAGIC 0x43565457UL /* "WTVC" little-endian */

#define WT_HSM_VAULT_FLAG_MASK \
    (WT_VAULT_FLAG_WRITE_ONCE | WT_VAULT_FLAG_NO_CONFIDENTIALITY | \
     WT_VAULT_FLAG_NO_REPLAY | WT_VAULT_FLAG_SEALED)

/* Rollback counter table (WT-FFM-0048), persisted at WT_HSM_VAULT_TABLE_ID.
 * global increments on every sealed write and is persisted BEFORE the sealed
 * object, so a power loss can never make a GCM nonce repeat; slot[] holds the
 * counter each in-window object was sealed under, so a replayed (rolled-back)
 * ciphertext fails tag authentication on the next read. */
typedef struct wt_hsm_vault_table {
    uint32_t magic;
    uint32_t reserved;
    uint64_t global;
    uint64_t slot[WT_HSM_VAULT_ID_COUNT];
} wt_hsm_vault_table_t;

static whNvmContext* g_vault_nvm;
static const wt_vault_sealer_t* g_vault_sealer;

/* Sealed-object staging, privileged vault domain only. */
static uint8_t g_vault_ct[WT_VAULT_OBJECT_MAX + WT_VAULT_SEAL_TAG_LEN];
static uint8_t g_vault_pt[WT_VAULT_OBJECT_MAX];

int wt_hsm_vault_init(whNvmContext* nvm)
{
    if (nvm == NULL) {
        return -1;
    }
    g_vault_nvm = nvm;
    return 0;
}

void wt_hsm_vault_set_sealer(const wt_vault_sealer_t* sealer)
{
    g_vault_sealer = sealer;
}

static void wt_hsm_vault_zeroize(uint8_t* buf, size_t len)
{
    volatile uint8_t* p = buf;
    size_t i;

    for (i = 0U; i < len; i++) {
        p[i] = 0U;
    }
}

static psa_status_t wt_hsm_vault_table_load(wt_hsm_vault_table_t* table)
{
    whNvmMetadata meta;
    int rc;

    rc = wh_Nvm_GetMetadata(g_vault_nvm, WT_HSM_VAULT_TABLE_ID, &meta);
    if (rc == WH_ERROR_NOTFOUND) {
        (void)memset(table, 0, sizeof(*table));
        table->magic = WT_HSM_VAULT_TABLE_MAGIC;
        return PSA_SUCCESS;
    }
    if (rc != WH_ERROR_OK || meta.len != sizeof(*table)) {
        return PSA_ERROR_STORAGE_FAILURE;
    }
    rc = wh_Nvm_Read(g_vault_nvm, WT_HSM_VAULT_TABLE_ID, 0U,
                     (whNvmSize)sizeof(*table), (uint8_t*)table);
    if (rc != WH_ERROR_OK || table->magic != WT_HSM_VAULT_TABLE_MAGIC) {
        return PSA_ERROR_STORAGE_FAILURE;
    }
    return PSA_SUCCESS;
}

static psa_status_t wt_hsm_vault_table_store(const wt_hsm_vault_table_t* table)
{
    whNvmMetadata meta;
    int rc;

    (void)memset(&meta, 0, sizeof(meta));
    meta.id = WT_HSM_VAULT_TABLE_ID;
    meta.access = WH_NVM_ACCESS_ANY;
    meta.flags = 0U;
    meta.len = (whNvmSize)sizeof(*table);
    rc = wh_Nvm_AddObject(g_vault_nvm, &meta, (whNvmSize)sizeof(*table),
                          (const uint8_t*)table);
    return (rc == WH_ERROR_OK) ? PSA_SUCCESS : PSA_ERROR_STORAGE_FAILURE;
}

void wt_hsm_vault_make_label(uint8_t* label, int32_t owner, int32_t sub,
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

uint32_t wt_hsm_vault_flags_of(const uint8_t* label)
{
    uint32_t flags;

    (void)memcpy(&flags, label + 16, sizeof(flags));
    return flags;
}

/* Shared directory lookup for privileged vault backends: find the
 * (owner, sub, uid) object in the vault id window. Returns PSA_SUCCESS
 * with the id + metadata, or PSA_ERROR_DOES_NOT_EXIST. out_free_id receives
 * the lowest unused id in the window (WH_NVM_ID_INVALID when full). */
psa_status_t wt_hsm_vault_lookup(int32_t owner, int32_t sub, uint64_t uid,
                                 whNvmId* out_id, whNvmMetadata* out_meta,
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
    wt_hsm_vault_table_t table;
    whNvmId id = WH_NVM_ID_INVALID;
    whNvmId free_id = WH_NVM_ID_INVALID;
    whNvmSize store_len;
    const uint8_t* store_data;
    psa_status_t status;

    if (g_vault_nvm == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    if (len > WT_VAULT_OBJECT_MAX ||
            (flags & ~(uint32_t)WT_HSM_VAULT_FLAG_MASK) != 0U) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if ((flags & WT_VAULT_FLAG_SEALED) != 0U && g_vault_sealer == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    status = wt_hsm_vault_lookup(owner, sub, uid, &id, &meta, &free_id);
    if (status == PSA_SUCCESS) {
        /* A storage SET must never overwrite a key object (WT-FFM-0046),
         * and honours WRITE_ONCE before any backend write; the *Checked add
         * enforces the same policy at the NVM layer. */
        if ((wt_hsm_vault_flags_of(meta.label) &
                (WT_VAULT_FLAG_KEY | WT_VAULT_FLAG_WRITE_ONCE)) != 0U) {
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
    wt_hsm_vault_make_label(meta.label, owner, sub, uid, flags);
    store_data = data;
    store_len = (whNvmSize)len;
    if ((flags & WT_VAULT_FLAG_SEALED) != 0U) {
        /* Persist the bumped counter before any ciphertext exists so a power
         * loss can never repeat a GCM nonce (WT-FFM-0048). */
        status = wt_hsm_vault_table_load(&table);
        if (status != PSA_SUCCESS) {
            return status;
        }
        table.global++;
        table.slot[id - WT_HSM_VAULT_ID_BASE] = table.global;
        status = wt_hsm_vault_table_store(&table);
        if (status != PSA_SUCCESS) {
            return status;
        }
        status = g_vault_sealer->seal(meta.label, WH_NVM_LABEL_LEN,
                                      table.global, data, len, g_vault_ct);
        if (status != PSA_SUCCESS) {
            return status;
        }
        store_data = g_vault_ct;
        store_len = (whNvmSize)(len + WT_VAULT_SEAL_TAG_LEN);
    }
    meta.len = store_len;
    return wt_hsm_vault_map_err(
        wh_Nvm_AddObjectChecked(g_vault_nvm, &meta, store_len, store_data));
}

static psa_status_t wt_hsm_vault_get(int32_t owner, int32_t sub,
                                     uint64_t uid, uint32_t offset,
                                     uint8_t* data, size_t size,
                                     size_t* out_len)
{
    whNvmMetadata meta;
    wt_hsm_vault_table_t table;
    whNvmId id = WH_NVM_ID_INVALID;
    size_t n;
    size_t pt_len;
    psa_status_t status;

    if (g_vault_nvm == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    status = wt_hsm_vault_lookup(owner, sub, uid, &id, &meta, NULL);
    if (status != PSA_SUCCESS) {
        return status;
    }
    if ((wt_hsm_vault_flags_of(meta.label) & WT_VAULT_FLAG_KEY) != 0U) {
        /* Key material is never readable through the storage face
         * (WT-FFM-0046); the NVM NONEXPORTABLE flag enforces the same at
         * the *Checked layer. */
        return PSA_ERROR_NOT_PERMITTED;
    }
    if ((wt_hsm_vault_flags_of(meta.label) & WT_VAULT_FLAG_SEALED) != 0U) {
        if (g_vault_sealer == NULL) {
            return PSA_ERROR_NOT_SUPPORTED;
        }
        if (meta.len < WT_VAULT_SEAL_TAG_LEN ||
                meta.len > sizeof(g_vault_ct)) {
            return PSA_ERROR_STORAGE_FAILURE;
        }
        status = wt_hsm_vault_map_err(
            wh_Nvm_ReadChecked(g_vault_nvm, id, 0U, meta.len, g_vault_ct));
        if (status != PSA_SUCCESS) {
            return status;
        }
        status = wt_hsm_vault_table_load(&table);
        if (status != PSA_SUCCESS) {
            return status;
        }
        if (table.slot[id - WT_HSM_VAULT_ID_BASE] == 0U) {
            /* A sealed object with no live counter is a rolled-back or
             * resurrected ciphertext — fail closed. */
            return PSA_ERROR_INVALID_SIGNATURE;
        }
        pt_len = (size_t)meta.len - WT_VAULT_SEAL_TAG_LEN;
        status = g_vault_sealer->unseal(
            meta.label, WH_NVM_LABEL_LEN,
            table.slot[id - WT_HSM_VAULT_ID_BASE], g_vault_ct, meta.len,
            g_vault_pt);
        if (status != PSA_SUCCESS) {
            return status;
        }
        if (offset > pt_len) {
            wt_hsm_vault_zeroize(g_vault_pt, pt_len);
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        n = pt_len - offset;
        if (n > size) {
            n = size;
        }
        if (n > 0U) {
            (void)memcpy(data, g_vault_pt + offset, n);
        }
        wt_hsm_vault_zeroize(g_vault_pt, pt_len);
        *out_len = n;
        return PSA_SUCCESS;
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
    status = wt_hsm_vault_lookup(owner, sub, uid, NULL, &meta, NULL);
    if (status != PSA_SUCCESS) {
        return status;
    }
    info->capacity = meta.len;
    info->size = meta.len;
    info->flags = wt_hsm_vault_flags_of(meta.label);
    if ((info->flags & WT_VAULT_FLAG_SEALED) != 0U) {
        if (meta.len < WT_VAULT_SEAL_TAG_LEN) {
            return PSA_ERROR_STORAGE_FAILURE;
        }
        info->capacity = meta.len - WT_VAULT_SEAL_TAG_LEN;
        info->size = info->capacity;
    }
    info->reserved = 0U;
    return PSA_SUCCESS;
}

static psa_status_t wt_hsm_vault_remove(int32_t owner, int32_t sub,
                                        uint64_t uid)
{
    whNvmMetadata meta;
    wt_hsm_vault_table_t table;
    whNvmId id = WH_NVM_ID_INVALID;
    psa_status_t status;

    if (g_vault_nvm == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    status = wt_hsm_vault_lookup(owner, sub, uid, &id, &meta, NULL);
    if (status != PSA_SUCCESS) {
        return status;
    }
    if ((wt_hsm_vault_flags_of(meta.label) &
            WT_VAULT_FLAG_WRITE_ONCE) != 0U) {
        return PSA_ERROR_NOT_PERMITTED;
    }
    if ((wt_hsm_vault_flags_of(meta.label) & WT_VAULT_FLAG_SEALED) != 0U) {
        /* Retire the counter first: a later flash-level resurrection of the
         * destroyed ciphertext then fails authentication (WT-FFM-0048). */
        status = wt_hsm_vault_table_load(&table);
        if (status != PSA_SUCCESS) {
            return status;
        }
        table.slot[id - WT_HSM_VAULT_ID_BASE] = 0U;
        status = wt_hsm_vault_table_store(&table);
        if (status != PSA_SUCCESS) {
            return status;
        }
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
