/* main.c
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

/* Compile-time parity between the wolfTrust PSA headers and the published PSA
 * Firmware Framework (DEN0063) and PSA Certified Status code values. A drift
 * here would let a caller mismatch a status or read a field at the wrong
 * offset, so the values are pinned rather than trusted. */

#include "psa/error.h"
#include "psa/service.h"
#include "psa/client.h"
#include "psa/lifecycle.h"

#include <stddef.h>
#include <stdio.h>

_Static_assert(PSA_SUCCESS == 0, "PSA_SUCCESS");
_Static_assert(PSA_ERROR_PROGRAMMER_ERROR == -129, "PROGRAMMER_ERROR");
_Static_assert(PSA_ERROR_CONNECTION_REFUSED == -130, "CONNECTION_REFUSED");
_Static_assert(PSA_ERROR_CONNECTION_BUSY == -131, "CONNECTION_BUSY");
_Static_assert(PSA_ERROR_GENERIC_ERROR == -132, "GENERIC_ERROR");
_Static_assert(PSA_ERROR_NOT_PERMITTED == -133, "NOT_PERMITTED");
_Static_assert(PSA_ERROR_NOT_SUPPORTED == -134, "NOT_SUPPORTED");
_Static_assert(PSA_ERROR_INVALID_ARGUMENT == -135, "INVALID_ARGUMENT");
_Static_assert(PSA_ERROR_INVALID_HANDLE == -136, "INVALID_HANDLE");
_Static_assert(PSA_ERROR_BAD_STATE == -137, "BAD_STATE");
_Static_assert(PSA_ERROR_BUFFER_TOO_SMALL == -138, "BUFFER_TOO_SMALL");
_Static_assert(PSA_ERROR_ALREADY_EXISTS == -139, "ALREADY_EXISTS");
_Static_assert(PSA_ERROR_DOES_NOT_EXIST == -140, "DOES_NOT_EXIST");
_Static_assert(PSA_ERROR_INSUFFICIENT_MEMORY == -141, "INSUFFICIENT_MEMORY");
_Static_assert(PSA_ERROR_INSUFFICIENT_STORAGE == -142, "INSUFFICIENT_STORAGE");
_Static_assert(PSA_ERROR_INSUFFICIENT_DATA == -143, "INSUFFICIENT_DATA");
_Static_assert(PSA_ERROR_SERVICE_FAILURE == -144, "SERVICE_FAILURE");
_Static_assert(PSA_ERROR_COMMUNICATION_FAILURE == -145, "COMMUNICATION_FAILURE");
_Static_assert(PSA_ERROR_STORAGE_FAILURE == -146, "STORAGE_FAILURE");
_Static_assert(PSA_ERROR_HARDWARE_FAILURE == -147, "HARDWARE_FAILURE");
_Static_assert(PSA_ERROR_INVALID_SIGNATURE == -149, "INVALID_SIGNATURE");
_Static_assert(PSA_ERROR_CORRUPTION_DETECTED == -151, "CORRUPTION_DETECTED");
_Static_assert(PSA_ERROR_DATA_CORRUPT == -152, "DATA_CORRUPT");
_Static_assert(PSA_ERROR_DATA_INVALID == -153, "DATA_INVALID");
_Static_assert(PSA_OPERATION_INCOMPLETE == -248, "OPERATION_INCOMPLETE");

_Static_assert(PSA_FRAMEWORK_VERSION == 0x0100U, "FRAMEWORK_VERSION 1.0");

_Static_assert(PSA_LIFECYCLE_PSA_STATE_MASK == 0xff00U, "PSA_STATE_MASK");
_Static_assert(PSA_LIFECYCLE_IMP_STATE_MASK == 0x00ffU, "IMP_STATE_MASK");
_Static_assert(PSA_LIFECYCLE_UNKNOWN == 0x0000U, "UNKNOWN");
_Static_assert(PSA_LIFECYCLE_SECURED == 0x3000U, "SECURED");

/* DEN0063 lays psa_msg_t out with type first and handle second; the SPM copies
 * the two together, so the order is load-bearing, not cosmetic. */
_Static_assert(offsetof(psa_msg_t, type) < offsetof(psa_msg_t, handle),
               "psa_msg_t type precedes handle");
_Static_assert(offsetof(psa_msg_t, type) == 0, "psa_msg_t type at offset 0");

int main(void)
{
    (void)printf("PSA header parity checks passed\n");
    return 0;
}
