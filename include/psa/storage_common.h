/* storage_common.h
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

/* PSA Certified Secure Storage API 1.0 common types (SRC-PSA-STORAGE). */

#ifndef PSA_STORAGE_COMMON_H
#define PSA_STORAGE_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "psa/error.h"

typedef uint64_t psa_storage_uid_t;
typedef uint32_t psa_storage_create_flags_t;

struct psa_storage_info_t {
    size_t capacity;
    size_t size;
    psa_storage_create_flags_t flags;
};

#define PSA_STORAGE_FLAG_NONE                 0U
#define PSA_STORAGE_FLAG_WRITE_ONCE           (1U << 0)
#define PSA_STORAGE_FLAG_NO_CONFIDENTIALITY   (1U << 1)
#define PSA_STORAGE_FLAG_NO_REPLAY_PROTECTION (1U << 2)

#define PSA_STORAGE_SUPPORT_SET_EXTENDED      (1U << 0)

#endif /* PSA_STORAGE_COMMON_H */
