/* monitor.c
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

#include "wolftrust/monitor.h"

#include <stdbool.h>

#ifdef WT_ENGINE_HSM
#include "wolftrust/sched/tasklet.h"
#include "wolftrust/services/hsm.h"
#endif

static wt_scheduler_state_t g_scheduler;

static wt_guest_runtime_t* wt_guest_runtime(wt_guest_id_t guest_id)
{
    if (guest_id >= g_scheduler.guest_count) {
        return NULL;
    }

    return &g_scheduler.runtime[guest_id];
}

static const wt_guest_config_t* wt_guest_config(wt_guest_id_t guest_id)
{
    if (guest_id >= g_scheduler.guest_count) {
        return NULL;
    }

    return &g_scheduler.configs[guest_id];
}

static wt_guest_id_t wt_find_next_runnable(wt_guest_id_t start)
{
    size_t attempts;

    for (attempts = 0; attempts < g_scheduler.guest_count; ++attempts) {
        wt_guest_id_t candidate = (start + attempts) % g_scheduler.guest_count;
        wt_guest_runtime_t* runtime = wt_guest_runtime(candidate);

        if (runtime == NULL) {
            continue;
        }
        if (runtime->remaining_delay_ticks > 0U) {
            continue;
        }
        if (runtime->state == WT_GUEST_READY ||
            runtime->state == WT_GUEST_RUNNING) {
            return candidate;
        }
    }

    return g_scheduler.guest_count;
}

static void wt_tick_restart_backoff(void)
{
    size_t i;

    for (i = 0; i < g_scheduler.guest_count; ++i) {
        wt_guest_runtime_t* runtime = &g_scheduler.runtime[i];

        if (runtime->remaining_delay_ticks > 0U) {
            runtime->remaining_delay_ticks--;
            if (runtime->remaining_delay_ticks == 0U &&
                runtime->state == WT_GUEST_RESTARTING) {
                wt_partition_reset_runtime(&g_scheduler.configs[i], runtime);
            }
        }
    }
}

static const wt_memory_window_t* wt_find_restart_clear_window(
    const wt_guest_config_t* config)
{
    size_t i;

    if (config == NULL) {
        return NULL;
    }

    for (i = 0; i < config->memory_window_count; ++i) {
        if ((config->memory_windows[i].attributes & WT_MEM_ATTR_RESTART_CLEAR) != 0U) {
            return &config->memory_windows[i];
        }
    }

    return NULL;
}

static void wt_apply_partition(wt_guest_id_t guest_id)
{
    const wt_guest_config_t* config = wt_guest_config(guest_id);

    if (config == NULL) {
        wt_platform_panic();
    }

    wt_platform_program_memory_windows(config->memory_windows,
                                       config->memory_window_count);
    wt_platform_program_ns_mpu(config->mpu_regions,
                               config->mpu_region_count);
    wt_platform_apply_irq_mask(&config->irq_mask);
}

static void wt_dispatch_guest(wt_guest_id_t guest_id)
{
    const wt_guest_config_t* config = wt_guest_config(guest_id);
    wt_guest_runtime_t* runtime = wt_guest_runtime(guest_id);

    if (config == NULL || runtime == NULL) {
        wt_platform_panic();
    }

    wt_apply_partition(guest_id);
    runtime->state = WT_GUEST_RUNNING;
    g_scheduler.current_guest = guest_id;
    wt_platform_start_secure_timer(config->timeslice_ms);
    wt_platform_prepare_guest_return(guest_id, &runtime->context);
    wt_platform_restore_guest_context(&runtime->context);
}

static void wt_save_running_guest(const wt_trap_frame_t* frame)
{
    wt_guest_runtime_t* current;
    wt_guest_state_t state;

    if (g_scheduler.guest_count == 0U) {
        return;
    }

    current = wt_guest_runtime(g_scheduler.current_guest);
    if (current != NULL &&
        (current->state == WT_GUEST_RUNNING ||
         current->state == WT_GUEST_WAITING_HSM)) {
        state = current->state;
        wt_platform_capture_guest_context(&current->context, frame);
        current->state = (state == WT_GUEST_WAITING_HSM) ?
                         WT_GUEST_WAITING_HSM : WT_GUEST_READY;
    }
}

static void wt_restart_guest(wt_guest_id_t guest_id, wt_fault_reason_t reason)
{
    const wt_guest_config_t* config = wt_guest_config(guest_id);
    wt_guest_runtime_t* runtime = wt_guest_runtime(guest_id);
    const wt_memory_window_t* restart_window;
    uint32_t restart_limit;
    uint32_t restart_window_ticks;

    if (config == NULL || runtime == NULL) {
        wt_platform_panic();
    }

    runtime->last_fault = reason;
    restart_limit = config->restart_policy.restart_limit;
    restart_window_ticks = config->restart_policy.restart_window_ticks;
        if (restart_limit > 0U) {
        if (runtime->restart_count == 0U ||
            (restart_window_ticks > 0U &&
             (g_scheduler.monotonic_ticks - runtime->first_restart_tick) >=
                 restart_window_ticks)) {
            runtime->restart_count = 0U;
            runtime->first_restart_tick = g_scheduler.monotonic_ticks;
        }
        if (runtime->restart_count >= restart_limit) {
            /* FAULTED is terminal until an external policy action resets or
             * reinitializes the partition. */
            runtime->state = WT_GUEST_FAULTED;
            return;
        }
    }

    if (runtime->restart_count == 0U) {
        runtime->first_restart_tick = g_scheduler.monotonic_ticks;
    }
    runtime->restart_count++;
    runtime->state = WT_GUEST_RESTARTING;
    runtime->remaining_delay_ticks =
        config->restart_policy.initial_delay_ticks;

    restart_window = wt_find_restart_clear_window(config);
    if (restart_window != NULL) {
        wt_platform_zero_guest_memory(restart_window->base, restart_window->size);
    }
}

static void wt_schedule_next_guest(void)
{
    wt_guest_id_t next_guest;

    next_guest = wt_find_next_runnable((g_scheduler.current_guest + 1U) %
                                       g_scheduler.guest_count);

    if (next_guest >= g_scheduler.guest_count) {
        wt_platform_all_guests_faulted();
    }

    wt_platform_quarantine_pending_irqs(&g_scheduler.configs[next_guest].irq_mask);
    wt_dispatch_guest(next_guest);
}

void wt_monitor_init(void)
{
    size_t count;
    size_t i;

    wt_platform_init();

    g_scheduler.configs = wt_partitions_config_table(&count);
    g_scheduler.runtime = wt_partitions_runtime_table(&count);
    g_scheduler.guest_count = count;
    g_scheduler.current_guest = 0U;
    g_scheduler.monotonic_ticks = 0U;

    for (i = 0; i < count; ++i) {
        g_scheduler.runtime[i].restart_count = 0U;
        g_scheduler.runtime[i].first_restart_tick = 0U;
        wt_partition_reset_runtime(&g_scheduler.configs[i], &g_scheduler.runtime[i]);
    }
}

void wt_monitor_start(void)
{
    wt_guest_id_t next_guest;

    wt_platform_mask_all_guest_irqs();
    next_guest = wt_find_next_runnable(0U);
    if (next_guest >= g_scheduler.guest_count) {
        wt_platform_all_guests_faulted();
    }

    wt_dispatch_guest(next_guest);
}

void wt_monitor_on_secure_timer(const wt_trap_frame_t* frame)
{
    g_scheduler.monotonic_ticks++;
    wt_platform_mask_all_guest_irqs();
#ifdef WT_ENGINE_HSM
    /* If SysTick interrupted secure-side HSM service code, the trap frame
     * represents secure execution — not a guest. Do not attempt guest
     * scheduling from that frame; return to the interrupted secure path and
     * let scheduling resume when the veneer returns. */
    if (wt_platform_secure_service_active() ||
        wt_tasklet_current() != (wt_tasklet_t *)0) {
        return;
    }
#endif
    wt_save_running_guest(frame);
    wt_tick_restart_backoff();
    wt_schedule_next_guest();
}

#ifdef WT_ENGINE_HSM
void wt_monitor_hsm_request_pending(wt_guest_id_t guest_id)
{
    wt_guest_runtime_t* runtime = wt_guest_runtime(guest_id);

    if (runtime == NULL ||
        runtime->state == WT_GUEST_FAULTED ||
        runtime->state == WT_GUEST_RESTARTING) {
        return;
    }

    runtime->state = WT_GUEST_WAITING_HSM;
}

void wt_monitor_hsm_response_ready(wt_guest_id_t guest_id)
{
    wt_guest_runtime_t* runtime = wt_guest_runtime(guest_id);

    if (runtime == NULL) {
        return;
    }
    if (runtime->state == WT_GUEST_WAITING_HSM) {
        runtime->state = (guest_id == g_scheduler.current_guest) ?
                         WT_GUEST_RUNNING : WT_GUEST_READY;
    }
}
#endif

void wt_monitor_on_guest_fault(const wt_trap_frame_t* frame,
                               wt_fault_reason_t reason)
{
    wt_guest_runtime_t* current = wt_guest_runtime(g_scheduler.current_guest);

    if (current == NULL) {
        wt_platform_panic();
    }

    wt_platform_mask_all_guest_irqs();
    wt_platform_capture_guest_context(&current->context, frame);
    wt_platform_log_fault(g_scheduler.current_guest,
                          reason,
                          wt_platform_read_fault_address(),
                          frame->pc);
    wt_restart_guest(g_scheduler.current_guest, reason);
    wt_schedule_next_guest();
}

const wt_scheduler_state_t* wt_monitor_state(void)
{
    return &g_scheduler;
}
