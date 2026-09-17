/* client.h
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

#ifndef PSA_CLIENT_H
#define PSA_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "psa/error.h"

typedef int32_t psa_handle_t;

typedef struct psa_invec {
    const void* base;
    size_t len;
} psa_invec;

typedef struct psa_outvec {
    void* base;
    size_t len;
} psa_outvec;

#define PSA_FRAMEWORK_VERSION      0x0100U
#define PSA_VERSION_NONE           0U
#define PSA_NULL_HANDLE            ((psa_handle_t)0)
#define PSA_MAX_IOVEC              4U
#define PSA_IPC_CALL               0
#define PSA_HANDLE_IS_VALID(handle) ((psa_handle_t)(handle) > 0)
#define PSA_HANDLE_TO_ERROR(handle) ((psa_status_t)(handle))

uint32_t psa_framework_version(void);
uint32_t psa_version(uint32_t sid);
psa_handle_t psa_connect(uint32_t sid, uint32_t version);
psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec* in_vec, size_t in_len,
                      psa_outvec* out_vec, size_t out_len);
void psa_close(psa_handle_t handle);

#endif /* PSA_CLIENT_H */
