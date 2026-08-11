/* crypto_service.h
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

#ifndef WOLFTRUST_SERVICES_CRYPTO_SERVICE_H
#define WOLFTRUST_SERVICES_CRYPTO_SERVICE_H

#include "wolftrust/ffm.h"

/* Copied-IOVEC bound (WT-FFM-0041): the SPM copies request input into a
 * bounded private buffer before invoking the isolated compute, so the Secure
 * Partition reads only its own stack. Requests larger than this are refused;
 * kept well under the 8 KiB per-SP secure stack. */
#define WT_CRYPTO_SP_INPUT_MAX 2048U

/* Isolated Secure Partition compute (WT-FFM-0011): SHA-256 over a private
 * input buffer into a private digest buffer. Makes no psa_* calls -- the SPM
 * performs every IOVEC transfer -- so it runs entirely on the Secure
 * Partition's own secure stack with the MPU narrowed to that domain. */
int wt_crypto_sp_hash(const uint8_t* input, size_t input_len,
                      uint8_t* digest, size_t digest_len);

/* SERVICE_CRYPTO's dispatch loop: wait, get, service one message, reply.
 * Architecture-neutral (no Armv8-M/CMSE dependency) so it is host-testable
 * through a real wt_ffm_connect/wt_ffm_call round trip. Supports a single
 * request today: PSA_IPC_CALL computes a SHA-256 digest of the input
 * vector into the output vector. */
int wt_crypto_service_dispatch(void* context, wt_ffm_runtime_t* runtime,
                               int32_t partition_id);

#endif /* WOLFTRUST_SERVICES_CRYPTO_SERVICE_H */
