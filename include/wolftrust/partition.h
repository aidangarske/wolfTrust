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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef WOLFTRUST_PARTITION_H
#define WOLFTRUST_PARTITION_H

#include "wolftrust/arch/armv8m/context.h"
#include "wolftrust/types.h"

/* Per-guest CMSE shared-buffer descriptor for the wolfHSM transport.
 * The buffer lives in the guest's NS RAM. The secure side validates
 * every byte access against this descriptor + cmse_check_address_range.
 * Both base and size are required; size==0 means this guest does not
 * have an HSM transport configured. */
typedef struct wt_hsm_transport_window {
    uintptr_t base;
    size_t    size;
} wt_hsm_transport_window_t;

typedef struct wt_guest_config {
    wt_guest_id_t guest_id;
    char name[WT_MAX_NAME_LEN];
    uintptr_t vector_table;
    uintptr_t initial_psp_ns;
    uintptr_t initial_msp_ns;
    wt_irq_mask_t irq_mask;
    wt_memory_window_t memory_windows[WT_MAX_MEMORY_WINDOWS];
    size_t memory_window_count;
    wt_mpu_region_t mpu_regions[WT_MAX_MPU_REGIONS];
    size_t mpu_region_count;
    wt_restart_policy_t restart_policy;
    uint32_t timeslice_ms;
    wt_hsm_transport_window_t hsm_transport;
} wt_guest_config_t;

typedef struct wt_guest_runtime {
    wt_guest_context_t context;
    wt_guest_state_t state;
    uint32_t remaining_delay_ticks;
    uint32_t restart_count;
    uint32_t first_restart_tick;
    wt_fault_reason_t last_fault;
} wt_guest_runtime_t;

typedef struct wt_guest_partition {
    wt_guest_config_t config;
    wt_guest_runtime_t runtime;
} wt_guest_partition_t;

const wt_guest_config_t* wt_partitions_config_table(size_t* count);
wt_guest_runtime_t* wt_partitions_runtime_table(size_t* count);
void wt_partition_reset_runtime(const wt_guest_config_t* config,
                                wt_guest_runtime_t* runtime);

#endif
