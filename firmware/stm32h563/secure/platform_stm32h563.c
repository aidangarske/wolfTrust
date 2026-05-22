/* platform_stm32h563.c
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

#include "wolftrust/platform.h"
#include "wolftrust/monitor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory_map.h"
#include "stm32h563_regs.h"

typedef struct wt_exception_frame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uintptr_t lr;
    uintptr_t pc;
    uint32_t xpsr;
} wt_exception_frame_t;

#define WT_ASM_STR2(x) #x
#define WT_ASM_STR(x) WT_ASM_STR2(x)

#define WT_GUEST_CONTEXT_PSP_NS_OFFSET     32U
#define WT_GUEST_CONTEXT_MSP_NS_OFFSET     36U
#define WT_GUEST_CONTEXT_CONTROL_NS_OFFSET 44U
#define WT_GUEST_CONTEXT_EXC_RETURN_OFFSET 48U

_Static_assert(WT_GUEST_CONTEXT_PSP_NS_OFFSET == 32U, "unexpected psp_ns offset");
_Static_assert(WT_GUEST_CONTEXT_MSP_NS_OFFSET == 36U, "unexpected msp_ns offset");
_Static_assert(WT_GUEST_CONTEXT_CONTROL_NS_OFFSET == 44U, "unexpected control_ns offset");
_Static_assert(WT_GUEST_CONTEXT_EXC_RETURN_OFFSET == 48U, "unexpected exc_return offset");
_Static_assert(WT_GUEST_CONTEXT_PSP_NS_OFFSET == offsetof(wt_guest_context_t, psp_ns),
               "wt_guest_context_t layout changed");
_Static_assert(WT_GUEST_CONTEXT_MSP_NS_OFFSET == offsetof(wt_guest_context_t, msp_ns),
               "wt_guest_context_t layout changed");
_Static_assert(WT_GUEST_CONTEXT_CONTROL_NS_OFFSET == offsetof(wt_guest_context_t, control_ns),
               "wt_guest_context_t layout changed");
_Static_assert(WT_GUEST_CONTEXT_EXC_RETURN_OFFSET == offsetof(wt_guest_context_t, exc_return),
               "wt_guest_context_t layout changed");

static wt_guest_context_t g_return_context;
static uint32_t g_live_r4_r11[8];
static uintptr_t g_live_exc_return;
static uint32_t g_systick_reload;
static uint32_t g_timeslice_ms;
static uintptr_t g_secure_entry_sp __attribute__((used));
static volatile uint32_t g_last_fault_address;
static volatile uint32_t g_last_fault_pc;
static volatile uint32_t g_switch_count;
static volatile uint32_t g_virtual_ms;
static volatile uint32_t g_active_guest;
static uint32_t g_ns_systick_csr[WT_MAX_GUESTS];

static void wt_sau_set_region(uint32_t rnr,
                              uint32_t base,
                              uint32_t limit_inclusive,
                              bool nsc)
{
    WT_SAU_RNR = rnr;
    WT_SAU_RBAR = base & 0xFFFFFFE0u;
    WT_SAU_RLAR = (limit_inclusive & 0xFFFFFFE0u) | (nsc ? 2u : 0u) | 1u;
}

static void wt_gtzc_init(void)
{
    size_t i;

    WT_RCC_AHB2ENR |= WT_RCC_AHB2ENR_GTZC1EN;

    for (i = 0; i < 16u; ++i) {
        WT_GTZC1_MPCBB1_SECCFGR[i] = 0x00000000u;
    }

    for (i = 0; i < 4u; ++i) {
        WT_GTZC1_MPCBB2_SECCFGR[i] = 0xFFFFFFFFu;
    }

    for (i = 0; i < 20u; ++i) {
        WT_GTZC1_MPCBB3_SECCFGR[i] = 0xFFFFFFFFu;
    }
}

static void wt_sau_init(void)
{
    wt_sau_set_region(0u, WT_FLASH_NS_BASE, WT_FLASH_NS_BASE + 0x001FFFFFu, false);
    wt_sau_set_region(1u, WT_RAM_NS_BASE, WT_RAM_NS_BASE + 0x0009FFFFu, false);
    wt_sau_set_region(2u, WT_FLASH_NSC_BASE, WT_FLASH_NSC_END, true);
    wt_sau_set_region(3u, 0x40000000u, 0x4FFFFFFFu, false);
    WT_SAU_CTRL = 1u;
    wt_dsb();
    wt_isb();
}

static uint32_t wt_read_psp_ns(void)
{
    uint32_t value;
    __asm volatile("mrs %0, psp_ns" : "=r"(value));
    return value;
}

static uint32_t wt_read_control_ns(void)
{
    uint32_t value;
    __asm volatile("mrs %0, control_ns" : "=r"(value));
    return value;
}

static uint32_t wt_read_ipsr(void)
{
    uint32_t value;
    __asm volatile("mrs %0, ipsr" : "=r"(value));
    return value;
}

static void wt_write_msp_ns(uint32_t value)
{
    __asm volatile("msr msp_ns, %0" :: "r"(value) : "memory");
}

static void wt_write_control_ns(uint32_t value)
{
    __asm volatile("msr control_ns, %0" :: "r"(value) : "memory");
}

static void wt_program_ns_mpu_region(uintptr_t base, size_t size, uint32_t attributes)
{
    uint32_t rbar = (uint32_t)(base & 0xFFFFFFE0u);
    uint32_t rlar = (uint32_t)(((base + size - 1u) & 0xFFFFFFE0u) | 0x1u);
    bool allow_write = (attributes & WT_MEM_ATTR_WRITE) != 0u;
    bool allow_read = (attributes & WT_MEM_ATTR_READ) != 0u;
    bool allow_exec = (attributes & WT_MEM_ATTR_EXEC) != 0u;
    bool is_device = (attributes & WT_MEM_ATTR_DEVICE) != 0u;

    if (!allow_exec) {
        rbar |= 0x1u;
    }
    if (allow_write) {
        rbar |= (0x1u << 1);
    } else if (allow_read) {
        rbar |= (0x3u << 1);
    }
    if (is_device) {
        rlar |= (0x1u << 1);
    }

    WT_MPU_NS_RBAR = rbar;
    WT_MPU_NS_RLAR = rlar;
}

static void wt_program_ns_mpu_regions(const wt_mpu_region_t* regions,
                                      size_t count)
{
    size_t i;

    WT_MPU_NS_CTRL = 0u;
    WT_MPU_NS_MAIR0 = 0x00000044u;

    for (i = 0; i < WT_MAX_MPU_REGIONS; ++i) {
        WT_MPU_NS_RNR = (uint32_t)i;
        if (regions != NULL && i < count && regions[i].size != 0u) {
            wt_program_ns_mpu_region(regions[i].base, regions[i].size,
                                     regions[i].attributes);
        } else {
            WT_MPU_NS_RBAR = 0u;
            WT_MPU_NS_RLAR = 0u;
        }
    }

    WT_MPU_NS_CTRL = 0x1u;
}

static void wt_exception_return_ns_msp(void) __attribute__((naked, noreturn));

static void wt_exception_return_ns_msp(void)
{
    __asm volatile(
        "ldr r2, =g_secure_entry_sp     \n"
        "ldr r2, [r2]                   \n"
        "mov sp, r2                     \n"
        "ldr r0, =g_return_context      \n"
        "ldmia r0, {r4-r11}             \n"
        "ldr r1, [r0, #" WT_ASM_STR(WT_GUEST_CONTEXT_PSP_NS_OFFSET) "] \n"
        "msr psp_ns, r1                 \n"
        "ldr r1, [r0, #" WT_ASM_STR(WT_GUEST_CONTEXT_MSP_NS_OFFSET) "] \n"
        "msr msp_ns, r1                 \n"
        "ldr r1, [r0, #" WT_ASM_STR(WT_GUEST_CONTEXT_CONTROL_NS_OFFSET) "] \n"
        "msr control_ns, r1             \n"
        "ldr lr, [r0, #" WT_ASM_STR(WT_GUEST_CONTEXT_EXC_RETURN_OFFSET) "] \n"
        "bx lr                          \n"
    );
}

typedef struct wt_guest_mailbox {
    volatile uint32_t boot_count;
    volatile uint32_t heartbeat;
    volatile uint32_t signature;
    volatile uint32_t virtual_ms;
    volatile uint32_t lines_printed;
    volatile uint32_t next_print_ms;
    volatile uint32_t run_token;
} wt_guest_mailbox_t;

static void wt_jump_to_ns(uint32_t msp_ns, uint32_t reset_addr) __attribute__((noreturn));

static void wt_jump_to_ns(uint32_t msp_ns, uint32_t reset_addr)
{
    wt_write_msp_ns(msp_ns);
    wt_write_control_ns(0u);
    __asm volatile("bxns %0" :: "r"(reset_addr) : "memory");
    __builtin_unreachable();
}

static void wt_maybe_finish_demo(void)
{
    volatile wt_guest_mailbox_t* g0 = (volatile wt_guest_mailbox_t*)WT_GUEST0_RAM_BASE;
    volatile wt_guest_mailbox_t* g1 = (volatile wt_guest_mailbox_t*)WT_GUEST1_RAM_BASE;

    if (g_virtual_ms < 10000u) {
        return;
    }

    if (g0->boot_count == 1u && g1->boot_count == 1u &&
        g0->lines_printed >= 5u && g1->lines_printed >= 5u) {
        __asm volatile("bkpt #0x7F");
    }
}

static void wt_update_virtual_time(void)
{
    g_virtual_ms += g_timeslice_ms;
    wt_maybe_finish_demo();
}

static void wt_secure_systick_dispatch(const wt_trap_frame_t* frame)
    __attribute__((noreturn, used));
static void wt_secure_fault_dispatch(const wt_trap_frame_t* frame)
    __attribute__((noreturn, used));

static void wt_secure_systick_dispatch(const wt_trap_frame_t* frame)
{
    wt_update_virtual_time();
    wt_monitor_on_secure_timer(frame);
    wt_platform_panic();
    __builtin_unreachable();
}

static void wt_secure_fault_dispatch(const wt_trap_frame_t* frame)
{
    g_last_fault_address = WT_SAU_SFAR;
    wt_monitor_on_guest_fault(frame, WT_FAULT_SECURE_ESCALATION);
    wt_platform_panic();
    __builtin_unreachable();
}

void wt_platform_init(void)
{
    WT_SCB_VTOR_S = WT_FLASH_S_BASE;
    wt_gtzc_init();
    wt_sau_init();
    /* Enable USART2 (guest 0) and USART3 (guest 1) clocks before guests run */
    WT_RCC_APB1LENR |= (1u << 17) | (1u << 18);
    wt_platform_zero_guest_memory(WT_GUEST0_RAM_BASE, WT_GUEST_RAM_SIZE);
    wt_platform_zero_guest_memory(WT_GUEST1_RAM_BASE, WT_GUEST_RAM_SIZE);
    g_switch_count = 0u;
    g_virtual_ms = 0u;
    g_timeslice_ms = 0u;
}

void WolfTrust_Yield_Impl(void) __attribute__((noreturn));

__attribute__((naked, section(".gnu.sgstubs")))
void WolfTrust_Yield(void)
{
    __asm volatile(
        "sg                        \n"
        "b.w WolfTrust_Yield_Impl  \n"
    );
}

void WolfTrust_Yield_Impl(void)
{
    __asm volatile("bkpt #0x70");
    wt_platform_panic();
    __builtin_unreachable();
}

void wt_platform_start_secure_timer(uint32_t timeslice_ms)
{
    uint32_t reload;

    if (timeslice_ms == 0u) {
        wt_platform_panic();
    }

    g_timeslice_ms = timeslice_ms;
    reload = timeslice_ms * (64000000u / 1000u);
    g_systick_reload = reload;
}

static void wt_arm_secure_timer(void)
{
    WT_SYST_CSR = 0u;
    WT_SYST_RVR = g_systick_reload - 1u;
    WT_SYST_CVR = 0u;
    WT_SYST_CSR = WT_SYST_CSR_CLKSOURCE |
                  WT_SYST_CSR_TICKINT |
                  WT_SYST_CSR_ENABLE;
}

void wt_platform_mask_all_guest_irqs(void)
{
    volatile uint32_t* icer = (volatile uint32_t*)0xE000E180u;
    size_t i;

    for (i = 0; i < WT_MAX_IRQ_WORDS; ++i) {
        icer[i] = 0xFFFFFFFFu;
    }
}

void wt_platform_apply_irq_mask(const wt_irq_mask_t* mask)
{
    volatile uint32_t* iser = (volatile uint32_t*)0xE000E100u;
    size_t i;

    if (mask == NULL) {
        return;
    }

    for (i = 0; i < WT_MAX_IRQ_WORDS; ++i) {
        iser[i] = mask->words[i];
    }
}

void wt_platform_quarantine_pending_irqs(const wt_irq_mask_t* allowed_mask)
{
    volatile uint32_t* icpr = (volatile uint32_t*)0xE000E280u;
    size_t i;

    if (allowed_mask == NULL) {
        return;
    }

    for (i = 0; i < WT_MAX_IRQ_WORDS; ++i) {
        icpr[i] = ~allowed_mask->words[i];
    }
}

void wt_platform_program_memory_windows(const wt_memory_window_t* windows,
                                        size_t count)
{
    (void)windows;
    (void)count;
}

void wt_platform_program_ns_mpu(const wt_mpu_region_t* regions, size_t count)
{
    wt_program_ns_mpu_regions(regions, count);
}

void wt_platform_prepare_guest_return(wt_guest_id_t guest_id,
                                      const wt_guest_context_t* context)
{
    uint32_t csr;

    if (context == NULL) {
        return;
    }

    /* Stop the departing guest's NS SysTick and save its settings */
    csr = WT_SYST_NS_CSR;
    if (g_active_guest < WT_MAX_GUESTS) {
        g_ns_systick_csr[g_active_guest] = csr;
    }
    WT_SYST_NS_CSR = 0u;

    WT_SCB_VTOR_NS = (uint32_t)context->vector_table_ns;
    g_active_guest = guest_id;
    WT_SCB_ICSR_NS = WT_SCB_ICSR_PENDSTCLR;

    /* Restart the arriving guest's NS SysTick from a clean 1ms countdown */
    if (guest_id < WT_MAX_GUESTS && (g_ns_systick_csr[guest_id] & WT_SYST_CSR_ENABLE)) {
        WT_SYST_NS_CVR = 0u;
        WT_SYST_NS_CSR = g_ns_systick_csr[guest_id];
    }
}

void wt_platform_capture_guest_context(wt_guest_context_t* context,
                                       const wt_trap_frame_t* frame)
{
    wt_exception_frame_t* stacked;

    if (context == NULL || frame == NULL) {
        wt_platform_panic();
    }

    stacked = (wt_exception_frame_t*)frame;
    context->psp_ns = wt_read_psp_ns();
    context->msp_ns = (uintptr_t)stacked;
    context->control_ns = wt_read_control_ns();
    context->exc_return = g_live_exc_return;
    context->r4_r11[0] = g_live_r4_r11[0];
    context->r4_r11[1] = g_live_r4_r11[1];
    context->r4_r11[2] = g_live_r4_r11[2];
    context->r4_r11[3] = g_live_r4_r11[3];
    context->r4_r11[4] = g_live_r4_r11[4];
    context->r4_r11[5] = g_live_r4_r11[5];
    context->r4_r11[6] = g_live_r4_r11[6];
    context->r4_r11[7] = g_live_r4_r11[7];
    context->pc = stacked->pc;
    context->lr = stacked->lr;
    context->xpsr = stacked->xpsr;
    context->frame_stacked = true;
}

void wt_platform_restore_guest_context(wt_guest_context_t* context)
{
    g_switch_count++;

    if (!context->frame_stacked) {
        volatile uint32_t* vtor = (volatile uint32_t*)context->vector_table_ns;

        if (wt_read_ipsr() == 0u) {
            wt_arm_secure_timer();
            wt_jump_to_ns(vtor[0], vtor[1]);
        } else {
            wt_exception_frame_t* stacked;

            context->msp_ns = (uintptr_t)vtor[0];
            context->pc = (uintptr_t)vtor[1];
            context->lr = 0u;
            context->xpsr = 0x01000000u;
            stacked = (wt_exception_frame_t*)(context->msp_ns - sizeof(wt_exception_frame_t));
            stacked->r0 = 0u;
            stacked->r1 = 0u;
            stacked->r2 = 0u;
            stacked->r3 = 0u;
            stacked->r12 = 0u;
            stacked->lr = context->lr;
            stacked->pc = context->pc;
            stacked->xpsr = context->xpsr;
            context->msp_ns = (uintptr_t)stacked;
            context->frame_stacked = true;
        }
    }

    g_return_context = *context;
    wt_arm_secure_timer();
    wt_exception_return_ns_msp();
}

void wt_platform_zero_guest_memory(uintptr_t base, size_t size)
{
    volatile uint32_t* ptr = (volatile uint32_t*)base;
    size_t words = size / sizeof(uint32_t);
    size_t i;

    for (i = 0; i < words; ++i) {
        ptr[i] = 0u;
    }
}

void wt_platform_log_fault(wt_guest_id_t guest_id,
                           wt_fault_reason_t reason,
                           uintptr_t fault_address,
                           uintptr_t pc)
{
    (void)guest_id;
    (void)reason;
    g_last_fault_address = (uint32_t)fault_address;
    g_last_fault_pc = (uint32_t)pc;
}

uintptr_t wt_platform_read_fault_address(void)
{
    return g_last_fault_address;
}

void wt_platform_all_guests_faulted(void)
{
    __asm volatile("bkpt #0x7D");
    for (;;) {
        __asm volatile("wfi");
    }
}

void wt_platform_panic(void)
{
    __asm volatile("bkpt #0x7E");
    for (;;) {
    }
}

void Reset_Handler(void)
{
    extern uint32_t _sidata;
    extern uint32_t _sdata;
    extern uint32_t _edata;
    extern uint32_t _sbss;
    extern uint32_t _ebss;
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;

    while (dst < &_edata) {
        *dst++ = *src++;
    }

    for (dst = &_sbss; dst < &_ebss; ++dst) {
        *dst = 0u;
    }

    wt_monitor_init();
    g_active_guest = 0u;
    wt_monitor_start();
    wt_platform_panic();
}

__attribute__((naked)) void SecureFault_Handler(void)
{
    __asm volatile(
        "mov r2, sp                     \n"
        "ldr r1, =g_secure_entry_sp     \n"
        "str r2, [r1]                   \n"
        "ldr r1, =g_live_r4_r11         \n"
        "stmia r1!, {r4-r11}            \n"
        "ldr r1, =g_live_exc_return     \n"
        "str lr, [r1]                   \n"
        "mrs r0, msp_ns                 \n"
        "b wt_secure_fault_dispatch     \n"
    );
}

__attribute__((naked)) void SysTick_Handler(void)
{
    __asm volatile(
        "mov r2, sp                     \n"
        "ldr r1, =g_secure_entry_sp     \n"
        "str r2, [r1]                   \n"
        "ldr r1, =g_live_r4_r11         \n"
        "stmia r1!, {r4-r11}            \n"
        "ldr r1, =g_live_exc_return     \n"
        "str lr, [r1]                   \n"
        "mrs r0, msp_ns                 \n"
        "b wt_secure_systick_dispatch   \n"
    );
}
