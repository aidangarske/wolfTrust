/* internal_trusted_storage.h
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

/* PSA Internal Trusted Storage API 1.0 (SRC-PSA-STORAGE), served by
 * SERVICE_ITS. Clients marshal these calls onto the SERVICE_ITS wire
 * protocol (wolftrust/services/storage_service.h) over FF-M IPC; the
 * NSPE client shim provides these function definitions. */

#ifndef PSA_INTERNAL_TRUSTED_STORAGE_H
#define PSA_INTERNAL_TRUSTED_STORAGE_H

#include "psa/storage_common.h"

#define PSA_ITS_API_VERSION_MAJOR 1
#define PSA_ITS_API_VERSION_MINOR 0

psa_status_t psa_its_set(psa_storage_uid_t uid, size_t data_length,
                         const void* p_data,
                         psa_storage_create_flags_t create_flags);

psa_status_t psa_its_get(psa_storage_uid_t uid, size_t data_offset,
                         size_t data_size, void* p_data,
                         size_t* p_data_length);

psa_status_t psa_its_get_info(psa_storage_uid_t uid,
                              struct psa_storage_info_t* p_info);

psa_status_t psa_its_remove(psa_storage_uid_t uid);

#endif /* PSA_INTERNAL_TRUSTED_STORAGE_H */
