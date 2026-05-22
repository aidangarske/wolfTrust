#include "wolftrust/partition.h"

#include <string.h>

#ifndef WT_TIMESLICE_MS
#define WT_TIMESLICE_MS 100U
#endif

#define WT_USART_REGION_SIZE 0x00000400U

static const wt_guest_config_t g_partition_configs[] = {
    {
        .guest_id = 0U,
        .name = "guest-a",
        .entry_point = 0x08002101U,
        .vector_table = 0x08002000U,
        .initial_psp_ns = 0x00000000U,
        .initial_msp_ns = 0x20004000U,
        .irq_mask = {
            .words = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
        },
        .memory_windows = {
            {0x08002000U, 0x00010000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20000000U, 0x00004000U,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_RESTART_CLEAR}
        },
        .memory_window_count = 2U,
        .mpu_regions = {
            {0x08002000U, 0x00010000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20000000U, 0x00004000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE},
            {0x40004400U, WT_USART_REGION_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_DEVICE}
        },
        .mpu_region_count = 3U,
        .restart_policy = {
            .restart_limit = 3U,
            .restart_window_ticks = 64U,
            .initial_delay_ticks = 1U
        },
        .timeslice_ms = WT_TIMESLICE_MS
    },
    {
        .guest_id = 1U,
        .name = "guest-b",
        .entry_point = 0x08012101U,
        .vector_table = 0x08012000U,
        .initial_psp_ns = 0x00000000U,
        .initial_msp_ns = 0x20008000U,
        .irq_mask = {
            .words = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
        },
        .memory_windows = {
            {0x08012000U, 0x00010000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20004000U, 0x00004000U,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_RESTART_CLEAR}
        },
        .memory_window_count = 2U,
        .mpu_regions = {
            {0x08012000U, 0x00010000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20004000U, 0x00004000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE},
            {0x40004800U, WT_USART_REGION_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_DEVICE}
        },
        .mpu_region_count = 3U,
        .restart_policy = {
            .restart_limit = 3U,
            .restart_window_ticks = 64U,
            .initial_delay_ticks = 1U
        },
        .timeslice_ms = WT_TIMESLICE_MS
    }
};

static wt_guest_runtime_t g_partition_runtime[
    sizeof(g_partition_configs) / sizeof(g_partition_configs[0])
];

const wt_guest_config_t* wt_partitions_config_table(size_t* count)
{
    if (count != NULL) {
        *count = sizeof(g_partition_configs) / sizeof(g_partition_configs[0]);
    }

    return g_partition_configs;
}

wt_guest_runtime_t* wt_partitions_runtime_table(size_t* count)
{
    if (count != NULL) {
        *count = sizeof(g_partition_runtime) / sizeof(g_partition_runtime[0]);
    }

    return g_partition_runtime;
}

void wt_partition_reset_runtime(const wt_guest_config_t* config,
                                wt_guest_runtime_t* runtime)
{
    uint32_t restart_count;
    uint32_t first_restart_tick;

    if (config == NULL || runtime == NULL) {
        return;
    }

    restart_count = runtime->restart_count;
    first_restart_tick = runtime->first_restart_tick;
    memset(runtime, 0, sizeof(*runtime));
    runtime->restart_count = restart_count;
    runtime->first_restart_tick = first_restart_tick;
    runtime->state = WT_GUEST_READY;
    runtime->context.psp_ns = config->initial_psp_ns;
    runtime->context.msp_ns = config->initial_msp_ns;
    runtime->context.vector_table_ns = config->vector_table;
    runtime->context.pc = config->entry_point;
    runtime->context.lr = 0U;
    runtime->context.xpsr = 0x01000000U;
    runtime->context.exc_return = 0xFFFFFFB8U;
    runtime->context.frame_stacked = false;
}
