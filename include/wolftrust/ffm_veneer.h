/* ffm_veneer.h
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

#ifndef WOLFTRUST_FFM_VENEER_H
#define WOLFTRUST_FFM_VENEER_H

#include <stdint.h>

/* Shared NS/Secure layout for the WolfTrust_FFM_Call veneer (item 3c). A
 * cmse_nonsecure_entry function cannot take stack-passed arguments (max
 * ~4 register args), so the full PSA_MAX_IOVEC vector set crosses the
 * boundary as one struct pointer. The secure side writes each out[].len
 * back with the bytes the service produced (psa_call outvec semantics).
 * Deliberately dependency-free (no ffm.h) so Non-secure callers -- the
 * Zephyr wolftrust-tee driver and guest apps -- can include just this. */
#define WT_FFM_VENEER_IOVEC_MAX 4U

typedef struct wt_ffm_veneer_invec {
    const void* base;
    uint32_t len;
} wt_ffm_veneer_invec_t;

typedef struct wt_ffm_veneer_outvec {
    void* base;
    uint32_t len;
} wt_ffm_veneer_outvec_t;

typedef struct wt_ffm_veneer_iovec {
    wt_ffm_veneer_invec_t in[WT_FFM_VENEER_IOVEC_MAX];
    wt_ffm_veneer_outvec_t out[WT_FFM_VENEER_IOVEC_MAX];
    uint32_t in_count;
    uint32_t out_count;
} wt_ffm_veneer_iovec_t;

#endif /* WOLFTRUST_FFM_VENEER_H */
