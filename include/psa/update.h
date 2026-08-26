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

/* PSA Firmware Update API 1.0 subset (SRC-PSA-FWU), served by SERVICE_FWU.
 * Clients marshal these calls onto the SERVICE_FWU wire protocol
 * (wolftrust/services/fwu_service.h) over FF-M IPC; the NSPE client shim
 * provides these function definitions. wolfTrust stages the candidate into
 * the wolfBoot update partition and arms the swap; the swapped image still
 * passes authenticated launch (WT-FFM-0049) and anti-rollback (WT-FFM-0050)
 * on the next boot. */

#ifndef PSA_UPDATE_H
#define PSA_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include "psa/error.h"

#define PSA_FWU_API_VERSION_MAJOR 1
#define PSA_FWU_API_VERSION_MINOR 0

/* psa_fwu_install may complete only after a reboot swaps the staged image. */
#define PSA_SUCCESS_REBOOT  ((psa_status_t)1)
#define PSA_SUCCESS_RESTART ((psa_status_t)2)

/* Component states (PSA FWU 1.0). wolfTrust drives READY -> WRITING ->
 * CANDIDATE -> STAGED and abort back to READY; TRIAL/REJECTED/UPDATED belong
 * to the post-reboot confirm flow that rides the full boot-and-update gate. */
#define PSA_FWU_READY     0u
#define PSA_FWU_WRITING   1u
#define PSA_FWU_CANDIDATE 2u
#define PSA_FWU_STAGED    3u
#define PSA_FWU_FAILED    4u
#define PSA_FWU_UPDATED   5u
#define PSA_FWU_TRIAL     6u
#define PSA_FWU_REJECTED  7u

typedef uint32_t psa_fwu_component_t;

typedef struct psa_fwu_component_info_t {
    uint32_t state;
    uint32_t version;
    uint32_t max_size;
    uint32_t staged_size;
} psa_fwu_component_info_t;

psa_status_t psa_fwu_query(psa_fwu_component_t component,
                           psa_fwu_component_info_t* info);

psa_status_t psa_fwu_start(psa_fwu_component_t component,
                           const void* manifest, size_t manifest_size);

psa_status_t psa_fwu_write(psa_fwu_component_t component, size_t image_offset,
                           const void* block, size_t block_size);

psa_status_t psa_fwu_finish(psa_fwu_component_t component);

psa_status_t psa_fwu_install(void);

psa_status_t psa_fwu_abort(psa_fwu_component_t component);

#endif /* PSA_UPDATE_H */
