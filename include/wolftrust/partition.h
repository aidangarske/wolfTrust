#ifndef WOLFTRUST_PARTITION_H
#define WOLFTRUST_PARTITION_H

#include "wolftrust/types.h"

typedef struct wt_guest_config {
    wt_guest_id_t guest_id;
    char name[WT_MAX_NAME_LEN];
    uintptr_t entry_point;
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
