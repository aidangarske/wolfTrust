/* guest_verify.h
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

#ifndef WOLFTRUST_GUEST_VERIFY_H
#define WOLFTRUST_GUEST_VERIFY_H

#include <stddef.h>
#include <stdint.h>

#define WT_GUEST_MEAS_DIGEST_SIZE 32u
#define WT_GUEST_MEAS_MAX_RECORDS 4u
#define WT_GUEST_MEAS_NAME_LEN    16u

/* One pinned guest measurement. Records live in a slot inside the signed
 * wolfTrust image, stamped by the image-assembly patcher before wolfBoot
 * signs, so the pinned digests share wolfTrust's own root of trust. */
typedef struct wt_guest_measurement {
    uint32_t guest_id;
    uint32_t version;
    uint32_t image_size;
    uint8_t digest[WT_GUEST_MEAS_DIGEST_SIZE];
} wt_guest_measurement_t;

typedef enum wt_guest_verify_result {
    WT_GUEST_VERIFY_OK = 0,
    WT_GUEST_VERIFY_ERROR_ARGUMENT = -700,
    WT_GUEST_VERIFY_ERROR_LAYOUT = -701,
    WT_GUEST_VERIFY_ERROR_DIGEST = -702,
    WT_GUEST_VERIFY_ERROR_VERSION = -703,
    WT_GUEST_VERIFY_ERROR_HASH = -704,
    WT_GUEST_VERIFY_ERROR_CAPACITY = -705
} wt_guest_verify_result_t;

/* WT-SYS-0002 / WT-FFM-0049 launch predicate: SHA-256 the guest image bytes,
 * pin them to the recorded digest, and enforce the manifest version floor.
 * Any failure means the guest must not be entered. */
int wt_guest_verify_image(const void* image,
                          size_t window_size,
                          const wt_guest_measurement_t* record,
                          uint32_t min_version);

/* Verified-launch measurement table consumed by Initial Attestation as the
 * per-guest software components. Only measurements that passed
 * wt_guest_verify_image may be recorded. */
int wt_guest_measurement_record(const wt_guest_measurement_t* record,
                                const char* name);
size_t wt_guest_measurement_count(void);
const wt_guest_measurement_t* wt_guest_measurement_get(size_t index,
                                                       const char** name);
void wt_guest_measurement_reset(void);

#endif
