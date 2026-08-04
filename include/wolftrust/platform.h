/* platform.h
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

#ifndef WOLFTRUST_PLATFORM_H
#define WOLFTRUST_PLATFORM_H

#include "wolftrust/types.h"

typedef struct wt_trap_frame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uintptr_t lr;
    uintptr_t pc;
    uint32_t xpsr;
} wt_trap_frame_t;

void wt_platform_init(void);
void wt_platform_start_secure_timer(uint32_t timeslice_ms);
void wt_platform_mask_all_guest_irqs(void);
void wt_platform_apply_irq_mask(const wt_irq_mask_t* mask);
void wt_platform_quarantine_pending_irqs(const wt_irq_mask_t* allowed_mask);
void wt_platform_program_memory_windows(const wt_memory_window_t* windows,
                                        size_t count);
void wt_platform_program_ns_mpu(const wt_mpu_region_t* regions, size_t count);
void wt_platform_prepare_guest_return(wt_guest_id_t guest_id,
                                      const wt_guest_context_t* context);
void wt_platform_capture_guest_context(wt_guest_context_t* context,
                                       const wt_trap_frame_t* frame);
void wt_platform_restore_guest_context(wt_guest_context_t* context);
void wt_platform_svc_guest_return(void) __attribute__((noreturn));
bool wt_platform_in_handler_mode(void);
bool wt_platform_ns_thread_mode_trap(void);
void wt_platform_return_to_secure_thread(
    void (*entry)(void) __attribute__((noreturn)));
void wt_platform_zero_guest_memory(uintptr_t base, size_t size);
void wt_platform_log_fault(wt_guest_id_t guest_id,
                           wt_fault_reason_t reason,
                           uintptr_t fault_address,
                           uintptr_t pc);
uintptr_t wt_platform_read_fault_address(void);
void wt_platform_all_guests_faulted(void) __attribute__((noreturn));
void wt_platform_panic(void) __attribute__((noreturn));
#ifdef WT_ENGINE_HSM
bool wt_platform_secure_service_active(void);
void wt_platform_note_hsm_wait_skip(wt_guest_id_t guest_id);
#endif

/* Returns the guest id whose context is currently active on the NS side,
 * or UINT32_MAX if no guest is running (boot, secure service, etc.).
 * Veneers must use this — never trust a guest-supplied VM id. */
uint32_t wt_platform_active_guest_id(void);

/* Mark an NVIC IRQ as targeting the non-secure world (ITNS bit) AND
 * enable it in the NVIC. Called once at boot for each synthetic vIRQ
 * the monitor wants to be deliverable to guests. */
void wt_platform_configure_ns_irq(uint32_t irq);

/* Assert (asserted=true) or deassert (asserted=false) an NS-targeted
 * IRQ via the NS alias of NVIC ISPR/ICPR. Idempotent. */
void wt_platform_set_ns_irq_pending(uint32_t irq, bool asserted);

#endif
