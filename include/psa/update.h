/* update.h
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

/* PSA Firmware Update API 1.0 (SRC-PSA-FWU), served by SERVICE_FWU. Clients
 * marshal these calls onto the SERVICE_FWU wire protocol
 * (wolftrust/services/fwu_service.h) over FF-M IPC. wolfTrust stages the
 * candidate into the wolfBoot update partition and arms the swap; the swapped
 * image still passes authenticated launch (WT-FFM-0049) and anti-rollback
 * (WT-FFM-0050) on the next boot, so installation commits at reboot and the
 * optional TRIAL flow is not offered (psa_fwu_accept reports the deviation). */

#ifndef PSA_UPDATE_H
#define PSA_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include "psa/error.h"

#define PSA_FWU_API_VERSION_MAJOR 1
#define PSA_FWU_API_VERSION_MINOR 0

#define PSA_ERROR_DEPENDENCY_NEEDED  ((psa_status_t)-156)
#define PSA_ERROR_FLASH_ABUSE        ((psa_status_t)-160)
#define PSA_ERROR_INSUFFICIENT_POWER ((psa_status_t)-161)

/* psa_fwu_install may complete only after a reboot swaps the staged image. */
#define PSA_SUCCESS_REBOOT  ((psa_status_t)1)
#define PSA_SUCCESS_RESTART ((psa_status_t)2)

typedef uint8_t psa_fwu_component_t;

typedef struct psa_fwu_image_version_t {
    uint8_t  major;
    uint8_t  minor;
    uint16_t patch;
    uint32_t build;
} psa_fwu_image_version_t;

/* Component states (PSA FWU 1.0). wolfTrust drives READY -> WRITING ->
 * CANDIDATE -> STAGED with FAILED on error; installation commits at the
 * reboot behind authenticated launch, so TRIAL/REJECTED/UPDATED never
 * persist across it. */
#define PSA_FWU_READY     0u
#define PSA_FWU_WRITING   1u
#define PSA_FWU_CANDIDATE 2u
#define PSA_FWU_STAGED    3u
#define PSA_FWU_FAILED    4u
#define PSA_FWU_TRIAL     5u
#define PSA_FWU_REJECTED  6u
#define PSA_FWU_UPDATED   7u

#define PSA_FWU_FLAG_VOLATILE_STAGING 0x00000001u
#define PSA_FWU_FLAG_ENCRYPTION       0x00000002u

/* wolfBoot programs the H563 flash in 16-byte quadwords. */
#define PSA_FWU_LOG2_WRITE_ALIGN 4u
/* Largest single psa_fwu_write block SERVICE_FWU accepts: the 1024-byte IPC
 * transfer budget minus the 16-byte marshalled request header, so a block of
 * exactly this size is deliverable through psa_call. */
#define PSA_FWU_MAX_WRITE_SIZE   1008u

typedef struct psa_fwu_impl_info_t {
    uint32_t staged_size;
} psa_fwu_impl_info_t;

typedef struct psa_fwu_component_info_t {
    uint8_t state;
    psa_status_t error;
    psa_fwu_image_version_t version;
    uint32_t max_size;
    uint32_t flags;
    uint32_t location;
    psa_fwu_impl_info_t impl;
} psa_fwu_component_info_t;

psa_status_t psa_fwu_query(psa_fwu_component_t component,
                           psa_fwu_component_info_t* info);

psa_status_t psa_fwu_start(psa_fwu_component_t component,
                           const void* manifest, size_t manifest_size);

psa_status_t psa_fwu_write(psa_fwu_component_t component, size_t image_offset,
                           const void* block, size_t block_size);

psa_status_t psa_fwu_finish(psa_fwu_component_t component);

psa_status_t psa_fwu_cancel(psa_fwu_component_t component);

psa_status_t psa_fwu_clean(psa_fwu_component_t component);

psa_status_t psa_fwu_install(void);

psa_status_t psa_fwu_request_reboot(void);

psa_status_t psa_fwu_reject(psa_status_t error);

psa_status_t psa_fwu_accept(void);

#endif /* PSA_UPDATE_H */
