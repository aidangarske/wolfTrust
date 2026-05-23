/* partitions.c
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

#include "wolftrust/partition.h"
#include "memory_map.h"

#include <string.h>

#ifndef WT_TIMESLICE_MS
#define WT_TIMESLICE_MS 100U
#endif

#define WT_USART_REGION_SIZE 0x00000400U
#if WT_SHARED_UART == 3
#define WT_GUEST0_USART_BASE 0x40004800U
#else
#define WT_GUEST0_USART_BASE 0x40004400U
#endif
#if WT_SHARED_UART == 1 || WT_SHARED_UART == 2
#define WT_GUEST1_USART_BASE 0x40004400U
#else
#define WT_GUEST1_USART_BASE 0x40004800U
#endif
#ifdef WT_HSM_DEMO
#define WT_GUEST_RESET_OFFSET 0x00000115U
#else
#define WT_GUEST_RESET_OFFSET 0x000002B1U
#endif
/* Initial restore runs from a Secure exception and returns to a Non-secure
 * Thread/MSP frame. ES must stay set because the exception was taken to Secure
 * state; clearing it trips INVPC on STM32H563 hardware. */
#define WT_EXC_RETURN_NS_THREAD_MSP_FROM_SECURE 0xFFFFFFB9U

static const wt_guest_config_t g_partition_configs[] = {
    {
        .guest_id = 0U,
        .name = "guest-a",
        .entry_point = WT_GUEST0_FLASH_BASE + WT_GUEST_RESET_OFFSET,
        .vector_table = WT_GUEST0_FLASH_BASE,
        .initial_psp_ns = 0x00000000U,
        .initial_msp_ns = 0x20004000U,
        .irq_mask = {
            .words = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
        },
        .memory_windows = {
            {WT_GUEST0_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20000000U, 0x00004000U,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_RESTART_CLEAR}
        },
        .memory_window_count = 2U,
        .mpu_regions = {
            {WT_GUEST0_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20000000U, 0x00004000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE},
            {WT_GUEST0_USART_BASE, WT_USART_REGION_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_DEVICE},
            /* NSC window: NS guests must be able to fetch the SG veneers.
             * Without this region the NS MPU blocks BL into 0x0C000400+
             * (the gateway). Per ARMv8-M, NSC fetches succeed when SAU
             * marks them NSC AND the NS MPU grants execute permission. */
            {WT_FLASH_NSC_BASE, (WT_FLASH_NSC_END - WT_FLASH_NSC_BASE + 1U),
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC}
        },
        .mpu_region_count = 4U,
        .restart_policy = {
            .restart_limit = 3U,
            .restart_window_ticks = 64U,
            .initial_delay_ticks = 1U
        },
        .timeslice_ms = WT_TIMESLICE_MS,
        .hsm_transport = { .base = WT_GUEST0_HSM_BUF_BASE, .size = WT_HSM_BUF_SIZE }
    },
    {
        .guest_id = 1U,
        .name = "guest-b",
        .entry_point = WT_GUEST1_FLASH_BASE + WT_GUEST_RESET_OFFSET,
        .vector_table = WT_GUEST1_FLASH_BASE,
        .initial_psp_ns = 0x00000000U,
        .initial_msp_ns = 0x20008000U,
        .irq_mask = {
            .words = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
        },
        .memory_windows = {
            {WT_GUEST1_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20004000U, 0x00004000U,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_RESTART_CLEAR}
        },
        .memory_window_count = 2U,
        .mpu_regions = {
            {WT_GUEST1_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20004000U, 0x00004000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE},
            {WT_GUEST1_USART_BASE, WT_USART_REGION_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_DEVICE},
            /* NSC window — see guest-a above. */
            {WT_FLASH_NSC_BASE, (WT_FLASH_NSC_END - WT_FLASH_NSC_BASE + 1U),
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC}
        },
        .mpu_region_count = 4U,
        .restart_policy = {
            .restart_limit = 3U,
            .restart_window_ticks = 64U,
            .initial_delay_ticks = 1U
        },
        .timeslice_ms = WT_TIMESLICE_MS,
        .hsm_transport = { .base = WT_GUEST1_HSM_BUF_BASE, .size = WT_HSM_BUF_SIZE }
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
    runtime->context.exc_return = WT_EXC_RETURN_NS_THREAD_MSP_FROM_SECURE;
    runtime->context.frame_stacked = false;
}
