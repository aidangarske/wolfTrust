/* rollback.h
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

#ifndef WOLFTRUST_ROLLBACK_H
#define WOLFTRUST_ROLLBACK_H

#include <stdint.h>

#include "wolftrust/guest_verify.h"

#define WT_ROLLBACK_TABLE_MAGIC 0x57545246u

/* Monotonic version floors persisted in the wolfHSM vault NVM (WT-FFM-0050).
 * Slot zero is the wolfTrust image floor; guest slots are indexed by
 * guest id and bound to the launch-pinned record versions. */
typedef struct wt_rollback_table {
    uint32_t magic;
    uint32_t reserved;
    uint32_t image_floor;
    uint32_t guest_floor[WT_GUEST_MEAS_MAX_RECORDS];
} wt_rollback_table_t;

typedef enum wt_rollback_result {
    WT_ROLLBACK_OK = 0,
    WT_ROLLBACK_REFUSED = -800,
    WT_ROLLBACK_ERROR_ARGUMENT = -801
} wt_rollback_result_t;

/* Accept a version only when it meets the stored floor. The unlocked
 * provisioning lifecycles (assembly-and-test, PSA RoT provisioning) bypass
 * refusal so development flows are never bricked by a floor; every other
 * lifecycle, including unknown, enforces strictly. */
int wt_rollback_check(uint32_t lifecycle,
                      uint32_t version,
                      uint32_t floor);

/* Raise *floor to version when the boot proved a newer image. Floors only
 * move forward; returns nonzero when the table changed and must persist. */
int wt_rollback_advance(uint32_t version, uint32_t* floor);

/* Reset a loaded table to first-boot state when it is absent or invalid. */
void wt_rollback_table_init(wt_rollback_table_t* table);
int wt_rollback_table_valid(const wt_rollback_table_t* table);

#endif
