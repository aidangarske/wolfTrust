/* ffm_crypto_client.c
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

/* OS-neutral non-secure crypto-service client helpers (Phase 7). Built on
 * the neutral PSA client core (psa_ffm_client.c), so every request is
 * mediated by the SPM — no OS headers, no direct secure transport. */

#include <stddef.h>
#include <stdint.h>

#include "psa/client.h"
#include "wolftrust/ffm_crypto_client.h"
#include "wolftrust/services/crypto_service.h"

int wt_ffm_crypto_random(uint32_t sid, uint8_t* out, size_t len)
{
    psa_handle_t handle;
    psa_outvec out_vec;
    psa_status_t status;
    size_t chunk;
    size_t off = 0U;
    int ret = 0;

    if (out == NULL || len == 0U) {
        return -1;
    }
    handle = psa_connect(sid, 1U);
    if (handle <= 0) {
        return -1;
    }
    while (off < len) {
        chunk = len - off;
        if (chunk > WT_CRYPTO_RANDOM_MAX) {
            chunk = WT_CRYPTO_RANDOM_MAX;
        }
        out_vec.base = out + off;
        out_vec.len = chunk;
        status = psa_call(handle, WT_CRYPTO_OP_RANDOM, NULL, 0U, &out_vec,
                          1U);
        if (status != PSA_SUCCESS || out_vec.len != chunk) {
            ret = -1;
            break;
        }
        off += chunk;
    }
    psa_close(handle);
    return ret;
}
