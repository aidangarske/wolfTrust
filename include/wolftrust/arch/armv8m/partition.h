/* partition.h
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

#ifndef WOLFTRUST_ARCH_ARMV8M_PARTITION_H
#define WOLFTRUST_ARCH_ARMV8M_PARTITION_H

#include "wolftrust/arch/armv8m/context.h"
#include "wolftrust/partition.h"

struct wt_guest_runtime {
    wt_guest_context_t context;
    wt_guest_state_t state;
    uint32_t remaining_delay_ticks;
    uint32_t restart_count;
    uint32_t first_restart_tick;
    wt_fault_reason_t last_fault;
};

typedef struct wt_guest_partition {
    wt_guest_config_t config;
    wt_guest_runtime_t runtime;
} wt_guest_partition_t;

#endif
