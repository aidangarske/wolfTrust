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
 * ~4 register args), so the single input/output vector pair crosses the
 * boundary as one struct pointer instead of four scalars. Deliberately
 * dependency-free (no ffm.h) so Non-secure callers -- the Zephyr
 * wolftrust-tee driver and guest apps -- can include just this. */
typedef struct wt_ffm_veneer_iovec {
    const void* input;
    uint32_t input_len;
    void* output;
    uint32_t output_len;
} wt_ffm_veneer_iovec_t;

#endif /* WOLFTRUST_FFM_VENEER_H */
