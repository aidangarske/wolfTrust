#include "wolftrust/platform.h"

#include <stddef.h>

static wt_irq_mask_t g_last_mask;
static wt_guest_context_t g_last_return;
static uintptr_t g_fault_address;
static uint32_t g_last_timer_ticks;

void wt_platform_init(void)
{
    g_fault_address = 0U;
    g_last_timer_ticks = 0U;
}

void wt_platform_start_secure_timer(uint32_t timeslice_ms)
{
    g_last_timer_ticks = timeslice_ms;
}

void wt_platform_mask_all_guest_irqs(void)
{
}

void wt_platform_apply_irq_mask(const wt_irq_mask_t* mask)
{
    if (mask != NULL) {
        g_last_mask = *mask;
    }
}

void wt_platform_quarantine_pending_irqs(const wt_irq_mask_t* allowed_mask)
{
    (void)allowed_mask;
}

void wt_platform_program_memory_windows(const wt_memory_window_t* windows,
                                        size_t count)
{
    (void)windows;
    (void)count;
}

void wt_platform_program_ns_mpu(const wt_mpu_region_t* regions, size_t count)
{
    (void)regions;
    (void)count;
}

void wt_platform_prepare_guest_return(wt_guest_id_t guest_id,
                                      const wt_guest_context_t* context)
{
    (void)guest_id;
    if (context != NULL) {
        g_last_return = *context;
    }
}

void wt_platform_capture_guest_context(wt_guest_context_t* context,
                                       const wt_trap_frame_t* frame)
{
    if (context == NULL || frame == NULL) {
        return;
    }

    context->pc = frame->pc;
    context->lr = frame->lr;
    context->xpsr = frame->xpsr;
}

void wt_platform_restore_guest_context(wt_guest_context_t* context)
{
    (void)context;
}

void wt_platform_zero_guest_memory(uintptr_t base, size_t size)
{
    (void)base;
    (void)size;
}

void wt_platform_log_fault(wt_guest_id_t guest_id,
                           wt_fault_reason_t reason,
                           uintptr_t fault_address,
                           uintptr_t pc)
{
    (void)guest_id;
    (void)reason;
    g_fault_address = fault_address;
    (void)pc;
}

uintptr_t wt_platform_read_fault_address(void)
{
    return g_fault_address;
}

void wt_platform_all_guests_faulted(void)
{
    for (;;) {
    }
}

void wt_platform_panic(void)
{
    for (;;) {
    }
}
