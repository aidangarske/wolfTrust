/* monitor.h
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

#ifndef WOLFTRUST_MONITOR_H
#define WOLFTRUST_MONITOR_H

#include "wolftrust/arch/armv8m/partition.h"
#include "wolftrust/platform.h"

typedef enum wt_scheduler_rep {
    WT_SCHED_REP_NS = 0,
    WT_SCHED_REP_HSM
} wt_scheduler_rep_t;

typedef struct wt_scheduler_state {
    const wt_guest_config_t* configs;
    wt_guest_runtime_t* runtime;
    size_t guest_count;
    wt_guest_id_t current_guest;
    wt_scheduler_rep_t current_rep;
    uint32_t monotonic_ticks;
} wt_scheduler_state_t;

void wt_monitor_init(void);
void wt_monitor_start(void);
void wt_monitor_on_secure_timer(const wt_trap_frame_t* frame);
void wt_monitor_on_guest_fault(const wt_trap_frame_t* frame,
                               wt_fault_reason_t reason);
const wt_scheduler_state_t* wt_monitor_state(void);

#ifdef WT_ENGINE_HSM
void wt_monitor_hsm_request_pending(wt_guest_id_t guest_id);
void wt_monitor_hsm_response_ready(wt_guest_id_t guest_id);
#endif

#endif
