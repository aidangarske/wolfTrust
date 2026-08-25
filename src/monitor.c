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

#include "wolftrust/ffm_boot.h"
#include "wolftrust/monitor.h"
#include "wolftrust/restart_policy.h"
#include "wolftrust/spm.h"
#include "wolftrust_manifest_generated.h"

#include <stdbool.h>

#ifdef WT_ENGINE_HSM
#include "wolftrust/sched/tasklet.h"
#include "wolftrust/services/hsm.h"
#endif
#ifdef CONFIG_VNET
#include "wolftrust/services/vnet_service.h"
#endif

static wt_scheduler_state_t g_scheduler;
static wt_spm_t g_spm;
/* Restart-engine event counters: non-static so the hardware harness can read
 * them by symbol over the debug port (UART markers can interleave-split). */
volatile uint32_t g_wt_restart_events;
volatile uint32_t g_wt_quarantine_events;
#ifdef WT_ENGINE_HSM
static wt_guest_id_t g_pending_tasklet_guest;
static bool g_pending_tasklet_guest_valid;
#endif

static void wt_schedule_next_guest(void) __attribute__((noreturn));
#ifdef WT_ENGINE_HSM
static void wt_dispatch_hsm_tasklet(wt_guest_id_t guest_id);
#endif

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

static bool wt_hsm_tasklet_runnable(wt_guest_id_t guest_id)
{
#ifdef WT_ENGINE_HSM
    wt_tasklet_t *tasklet = wt_hsm_guest_tasklet(guest_id);

    if (tasklet == NULL) {
        return false;
    }

    /* A wake that landed while the tasklet was tick-preempted between its
     * NOTREADY poll and its block is latched, not delivered: promote it
     * here so the request it announced is served on the next dispatch. */
    if (wt_tasklet_state(tasklet) == WT_TASKLET_BLOCKED &&
            wt_tasklet_wake_pending(tasklet)) {
        wt_tasklet_wake(tasklet);
    }

    return wt_tasklet_state(tasklet) == WT_TASKLET_RUNNABLE;
#else
    (void)guest_id;
    return false;
#endif
}

#ifdef WT_ENGINE_HSM
static void wt_resume_pending_tasklet_guest(void) __attribute__((noreturn));
static void wt_resume_pending_tasklet_guest(void)
{
    wt_guest_id_t guest_id;

    if (!g_pending_tasklet_guest_valid) {
        wt_platform_panic();
    }

    guest_id = g_pending_tasklet_guest;
    g_pending_tasklet_guest_valid = false;
    wt_dispatch_hsm_tasklet(guest_id);
    wt_schedule_next_guest();
}
#endif

static wt_guest_id_t wt_find_next_runnable(wt_guest_id_t start,
                                           wt_scheduler_rep_t *rep)
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
        if (runtime->state == WT_GUEST_WAITING_HSM) {
#ifdef WT_ENGINE_HSM
            if (wt_hsm_tasklet_runnable(candidate)) {
                if (rep != NULL) {
                    *rep = WT_SCHED_REP_HSM;
                }
                return candidate;
            }
            wt_platform_note_hsm_wait_skip(candidate);
#endif
            continue;
        }
        if (runtime->state == WT_GUEST_READY ||
            runtime->state == WT_GUEST_RUNNING) {
            if (rep != NULL) {
                *rep = WT_SCHED_REP_NS;
            }
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
    /* Per-guest irq_mask is authoritative. Guests that want IRQ-driven
     * VNET RX must list WT_VNET_RX_IRQ in their partition config; the
     * dispatch-time reflection in wt_vnet_service_refresh_irq still
     * maintains the pending bit either way, so poll-only guests work
     * via vnet_rx_poll without touching the NVIC. */
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
    g_scheduler.current_rep = WT_SCHED_REP_NS;
#ifdef CONFIG_VNET
    wt_vnet_service_refresh_irq(guest_id);
#endif
    wt_platform_start_secure_timer(config->timeslice_ms);
    wt_platform_prepare_guest_return(guest_id, runtime->context);
    wt_platform_restore_guest_context(runtime->context);
}

#ifdef WT_ENGINE_HSM
static void wt_dispatch_hsm_tasklet(wt_guest_id_t guest_id)
{
    const wt_guest_config_t* config = wt_guest_config(guest_id);
    wt_guest_runtime_t* runtime = wt_guest_runtime(guest_id);
    wt_tasklet_t *tasklet;

    if (config == NULL || runtime == NULL ||
        runtime->state != WT_GUEST_WAITING_HSM) {
        wt_platform_panic();
    }

    tasklet = wt_hsm_guest_tasklet(guest_id);
    if (tasklet == NULL) {
        wt_platform_panic();
    }

    wt_apply_partition(guest_id);
    g_scheduler.current_guest = guest_id;
    g_scheduler.current_rep = WT_SCHED_REP_HSM;
    wt_platform_start_secure_timer(config->timeslice_ms);
    /* The tasklet completes back into this guest's NS thread via BXNS, not an
     * exception return, so its NS bank must be reinstated here or it resumes
     * on the previous guest's CONTROL_NS/MSP_NS. */
    wt_platform_restore_ns_bank(runtime->context);
    (void)wt_tasklet_resume(tasklet);
}
#endif

static void wt_save_running_guest(const wt_trap_frame_t* frame)
{
    wt_guest_runtime_t* current;
    wt_guest_state_t state;

    if (g_scheduler.guest_count == 0U) {
        return;
    }

    if (g_scheduler.current_rep != WT_SCHED_REP_NS) {
        return;
    }

    current = wt_guest_runtime(g_scheduler.current_guest);
    if (current != NULL &&
        (current->state == WT_GUEST_RUNNING ||
         current->state == WT_GUEST_WAITING_HSM)) {
        state = current->state;
        wt_platform_capture_guest_context(current->context, frame);
        current->state = (state == WT_GUEST_WAITING_HSM) ?
                         WT_GUEST_WAITING_HSM : WT_GUEST_READY;
    }
}

static void wt_restart_guest(wt_guest_id_t guest_id, wt_fault_reason_t reason)
{
    const wt_guest_config_t* config = wt_guest_config(guest_id);
    wt_guest_runtime_t* runtime = wt_guest_runtime(guest_id);
    const wt_memory_window_t* restart_window;

    if (config == NULL || runtime == NULL) {
        wt_platform_panic();
    }

    runtime->last_fault = reason;
    if (wt_restart_policy_evaluate(config->restart_policy.restart_limit,
                                   config->restart_policy.restart_window_ticks,
                                   g_scheduler.monotonic_ticks,
                                   &runtime->restart_count,
                                   &runtime->first_restart_tick) ==
            WT_RESTART_DECISION_FAULT) {
        /* FAULTED is terminal until an external policy action resets or
         * reinitializes the partition. */
        runtime->state = WT_GUEST_FAULTED;
        g_wt_quarantine_events++;
        return;
    }

    runtime->state = WT_GUEST_RESTARTING;
    g_wt_restart_events++;
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
    wt_scheduler_rep_t rep = WT_SCHED_REP_NS;

    if (g_scheduler.guest_count == 0U) {
        wt_platform_all_guests_faulted();
    }

    for (;;) {
        next_guest = wt_find_next_runnable((g_scheduler.current_guest + 1U) %
                                           g_scheduler.guest_count,
                                           &rep);

        if (next_guest >= g_scheduler.guest_count) {
            wt_platform_all_guests_faulted();
        }

        wt_platform_quarantine_pending_irqs(&g_scheduler.configs[next_guest].irq_mask);
        if (rep == WT_SCHED_REP_NS) {
            wt_dispatch_guest(next_guest);
        }
#ifdef WT_ENGINE_HSM
        else {
            if (wt_platform_in_handler_mode()) {
                g_pending_tasklet_guest = next_guest;
                g_pending_tasklet_guest_valid = true;
                wt_platform_return_to_secure_thread(wt_resume_pending_tasklet_guest);
            }

            wt_dispatch_hsm_tasklet(next_guest);
        }
#endif
    }
}

void wt_monitor_init(void)
{
    size_t count;
    size_t i;
    int spm_result;

    wt_platform_init();

    spm_result = wt_spm_init(&g_spm, wt_generated_manifest_get(),
                             WT_MANIFEST_FEATURE_IPC,
                             wt_partitions_profile_capabilities());
    if (spm_result != WT_SPM_VALID) {
        wt_platform_panic();
    }

    g_scheduler.configs = wt_partitions_config_table(&count);
    g_scheduler.runtime = wt_partitions_runtime_table(&count);
    if (wt_partitions_bind_manifest(wt_spm_manifest(&g_spm)) != 0) {
        wt_platform_panic();
    }
    if (wt_ffm_boot_init(wt_spm_manifest(&g_spm)) != WT_FFM_SUCCESS) {
        wt_platform_panic();
    }
    g_scheduler.guest_count = count;
    g_scheduler.current_guest = 0U;
    g_scheduler.current_rep = WT_SCHED_REP_NS;
    g_scheduler.monotonic_ticks = 0U;
#ifdef WT_ENGINE_HSM
    g_pending_tasklet_guest = 0U;
    g_pending_tasklet_guest_valid = false;
#endif
#ifdef CONFIG_VNET
    wt_vnet_service_init();
#endif

    for (i = 0; i < count; ++i) {
        g_scheduler.runtime[i].restart_count = 0U;
        g_scheduler.runtime[i].first_restart_tick = 0U;
        wt_partition_reset_runtime(&g_scheduler.configs[i], &g_scheduler.runtime[i]);
        if (!wt_platform_guest_context_ready(g_scheduler.runtime[i].context)) {
            wt_platform_panic();
        }
    }
}

void wt_monitor_start(void)
{
    wt_guest_id_t next_guest;

    wt_platform_mask_all_guest_irqs();
    next_guest = wt_find_next_runnable(0U, NULL);
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
    if (wt_tasklet_current() != (wt_tasklet_t *)0) {
        wt_guest_id_t tasklet_guest =
            wt_hsm_guest_for_tasklet(wt_tasklet_current());

        wt_tick_restart_backoff();
        /* Preempt only a genuine HSM tasklet that is physically executing
         * on its PSP. FF-M SP coroutines share this machinery but resolve
         * to no HSM guest, and a tick inside the bootstrap's switch window
         * (current already updated, PendSV not yet taken) would corrupt
         * the in-flight switch — the confboot silent-hang/INVPC flake. */
        if (tasklet_guest < g_scheduler.guest_count &&
            g_scheduler.runtime[tasklet_guest].state == WT_GUEST_WAITING_HSM &&
            wt_platform_secure_psp_thread_trap()) {
            (void)wt_tasklet_request_preempt();
        }
        return;
    }
    if (wt_platform_secure_service_active()) {
        return;
    }
#endif
    if (!wt_platform_ns_thread_mode_trap()) {
        wt_tick_restart_backoff();
        return;
    }
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
        runtime->state = WT_GUEST_READY;
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
    wt_platform_capture_guest_context(current->context, frame);
    wt_platform_log_fault(g_scheduler.current_guest,
                          reason,
                          wt_platform_read_fault_address(),
                          frame->pc);
#if defined(WT_CONFORMANCE) && (WT_CONFORMANCE == 1)
    /* The Arm suite's PROGRAMMER-ERROR checks that fault inside the NS client
     * (e.g. dereferencing a Secure address as an iovec array) expect a system
     * restart so val resumes off its flash boot flag; upstream platforms get
     * this from a PAL watchdog. Production keeps the graceful per-guest
     * restart below instead. */
    wt_platform_system_reset();
#endif
    wt_restart_guest(g_scheduler.current_guest, reason);
    wt_schedule_next_guest();
}

const wt_scheduler_state_t* wt_monitor_state(void)
{
    return &g_scheduler;
}
