/* types.h
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

#ifndef WOLFTRUST_TYPES_H
#define WOLFTRUST_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef WT_MAX_GUESTS
#define WT_MAX_GUESTS 2U
#endif
#define WT_MAX_IRQ_WORDS 8U
#define WT_MAX_MEMORY_WINDOWS 8U
#define WT_MAX_MPU_REGIONS 8U
#define WT_MAX_NAME_LEN 16U

#define WT_MEM_ATTR_READ          (1U << 0)
#define WT_MEM_ATTR_WRITE         (1U << 1)
#define WT_MEM_ATTR_EXEC          (1U << 2)
#define WT_MEM_ATTR_DEVICE        (1U << 3)
#define WT_MEM_ATTR_RESTART_CLEAR (1U << 4)

typedef uint32_t wt_guest_id_t;

typedef enum wt_guest_state {
    WT_GUEST_STOPPED = 0,
    WT_GUEST_READY,
    WT_GUEST_RUNNING,
    WT_GUEST_FAULTED,
    WT_GUEST_RESTARTING
} wt_guest_state_t;

typedef enum wt_fault_reason {
    WT_FAULT_NONE = 0,
    WT_FAULT_MEMORY_VIOLATION,
    WT_FAULT_ILLEGAL_INSTRUCTION,
    WT_FAULT_STACK_OVERFLOW,
    WT_FAULT_SECURE_ESCALATION,
    WT_FAULT_PLATFORM
} wt_fault_reason_t;

typedef struct wt_irq_mask {
    uint32_t words[WT_MAX_IRQ_WORDS];
} wt_irq_mask_t;

typedef struct wt_memory_window {
    uintptr_t base;
    size_t size;
    uint32_t attributes;
} wt_memory_window_t;

typedef struct wt_mpu_region {
    uintptr_t base;
    size_t size;
    uint32_t attributes;
} wt_mpu_region_t;

typedef struct wt_guest_context {
    uint32_t r4_r11[8];
    uintptr_t psp_ns;
    uintptr_t msp_ns;
    uintptr_t vector_table_ns;
    uint32_t control_ns;
    uintptr_t exc_return;
    uintptr_t pc;
    uintptr_t lr;
    uint32_t xpsr;
    bool frame_stacked;
    bool active_exception;
} wt_guest_context_t;

typedef struct wt_restart_policy {
    uint32_t restart_limit;
    uint32_t restart_window_ticks;
    uint32_t initial_delay_ticks;
} wt_restart_policy_t;

#endif
