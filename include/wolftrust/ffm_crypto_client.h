/* ffm_crypto_client.h
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

#ifndef WOLFTRUST_FFM_CRYPTO_CLIENT_H
#define WOLFTRUST_FFM_CRYPTO_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OS-neutral non-secure helper (WT-FFM-0053/0054): fill out[0..len) from
 * SERVICE_CRYPTO's vault-backed RNG over FF-M IPC, chunked at the service's
 * WT_CRYPTO_RANDOM_MAX bound. sid is the SERVICE_CRYPTO SID from the
 * platform manifest. Returns 0 on success, -1 on refusal or transport
 * failure. Suitable as the wolfCrypt seed/entropy hook on a guest whose
 * only path to entropy is the SPM. */
int wt_ffm_crypto_random(uint32_t sid, uint8_t* out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFTRUST_FFM_CRYPTO_CLIENT_H */
