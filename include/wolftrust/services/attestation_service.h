/* attestation_service.h
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

#ifndef WOLFTRUST_SERVICES_ATTESTATION_SERVICE_H
#define WOLFTRUST_SERVICES_ATTESTATION_SERVICE_H

#include "wolftrust/ffm.h"

/* SERVICE_ATTEST's dispatch loop: wait, get, service one message, reply.
 * Architecture-neutral (no Armv8-M/CMSE dependency) so it is host-testable
 * through a real wt_ffm_connect/wt_ffm_call round trip. A PSA_IPC_CALL carries
 * the caller's challenge in the input vector and returns the Initial
 * Attestation token in the output vector; the token itself is produced by the
 * existing wt_initial_attest_get_token backend. */
int wt_attestation_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                                    int32_t partition_id);

#endif /* WOLFTRUST_SERVICES_ATTESTATION_SERVICE_H */
