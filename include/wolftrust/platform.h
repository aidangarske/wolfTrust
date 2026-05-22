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
void wt_platform_zero_guest_memory(uintptr_t base, size_t size);
void wt_platform_log_fault(wt_guest_id_t guest_id,
                           wt_fault_reason_t reason,
                           uintptr_t fault_address,
                           uintptr_t pc);
uintptr_t wt_platform_read_fault_address(void);
void wt_platform_all_guests_faulted(void);
void wt_platform_panic(void);

#endif
