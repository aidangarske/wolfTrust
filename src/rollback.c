/* rollback.c
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

#include "wolftrust/rollback.h"

#include "psa/lifecycle.h"

#include <stddef.h>
#include <string.h>

static int wt_rollback_lifecycle_unlocked(uint32_t lifecycle)
{
    return (lifecycle == PSA_LIFECYCLE_ASSEMBLY_AND_TEST) ||
           (lifecycle == PSA_LIFECYCLE_PSA_ROT_PROVISIONING);
}

int wt_rollback_check(uint32_t lifecycle,
                      uint32_t version,
                      uint32_t floor)
{
    if (wt_rollback_lifecycle_unlocked(lifecycle)) {
        return WT_ROLLBACK_OK;
    }

    if (version < floor) {
        return WT_ROLLBACK_REFUSED;
    }

    return WT_ROLLBACK_OK;
}

int wt_rollback_advance(uint32_t version, uint32_t* floor)
{
    if (floor == NULL) {
        return 0;
    }

    if (version > *floor) {
        *floor = version;
        return 1;
    }

    return 0;
}

void wt_rollback_table_init(wt_rollback_table_t* table)
{
    if (table == NULL) {
        return;
    }

    (void)memset(table, 0, sizeof(*table));
    table->magic = WT_ROLLBACK_TABLE_MAGIC;
}

int wt_rollback_table_valid(const wt_rollback_table_t* table)
{
    return (table != NULL) && (table->magic == WT_ROLLBACK_TABLE_MAGIC);
}
