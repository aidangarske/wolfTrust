/* lifecycle.h
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

/* PSA FF-M lifecycle API (Arm FF-M 1.0 chapter 5). */

#ifndef PSA_LIFECYCLE_H
#define PSA_LIFECYCLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSA_LIFECYCLE_PSA_STATE_MASK              0xff00u
#define PSA_LIFECYCLE_IMP_STATE_MASK              0xffff0000u

#define PSA_LIFECYCLE_UNKNOWN                     0x0000u
#define PSA_LIFECYCLE_ASSEMBLY_AND_TEST           0x1000u
#define PSA_LIFECYCLE_PSA_ROT_PROVISIONING        0x2000u
#define PSA_LIFECYCLE_SECURED                     0x3000u
#define PSA_LIFECYCLE_NON_PSA_ROT_DEBUG           0x4000u
#define PSA_LIFECYCLE_RECOVERABLE_PSA_ROT_DEBUG   0x5000u
#define PSA_LIFECYCLE_DECOMMISSIONED              0x6000u

uint32_t psa_rot_lifecycle_state(void);

#ifdef __cplusplus
}
#endif

#endif /* PSA_LIFECYCLE_H */
