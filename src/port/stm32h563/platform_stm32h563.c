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
#include <wolfHAL/clock/stm32h5_rcc.h>
#include <wolfHAL/platform/st/stm32h563xx.h>
#include <wolfHAL/reg.h>

#include "memory_map.h"
#include "stm32h563_regs.h"

#ifdef WT_HSM_DEMO
#include "wolftrust/services/hsm.h"
#include "wolftrust/arch/armv8m/cmse.h"
#include "wolftrust/arch/armv8m/cmse_transport.h"
#include "wolftrust/sched/coroutine.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_transport_mem.h"

#define WT_HSM_YIELD_BUDGET  16u
#define WT_HSM_SUBMIT_BUDGET  8u
#endif /* WT_HSM_DEMO */

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

/* Referenced by inline asm in SysTick_Handler; mark used so -Os does
 * not DCE them since the C code only touches them by name in __asm. */
static wt_guest_context_t g_return_context __attribute__((used));
static uint32_t g_live_r4_r11[8] __attribute__((used));
static uintptr_t g_live_exc_return __attribute__((used));
static uint32_t g_systick_reload;
static uint32_t g_timeslice_ms;
static uintptr_t g_secure_entry_sp __attribute__((used));
static volatile uint32_t g_last_fault_address;
static volatile uint32_t g_last_fault_pc;
static volatile uint32_t g_switch_count;
static volatile uint32_t g_virtual_ms;
static volatile uint32_t g_active_guest;
static uint32_t g_ns_systick_csr[WT_MAX_GUESTS];

static void wt_rcc_enable_clock(uintptr_t base,
                                const whal_Stm32h5_Rcc_PeriphClk* clk)
{
    whal_Reg_Update((size_t)base, clk->regOffset, clk->enableMask,
                    clk->enableMask);
}

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
        WT_GTZC1_MPCBB1_SECCFGR[i] = 0xFFFFFFFFu;
    }

    /* SRAM1 MPCBB blocks are 512 B. The two guest windows occupy the first
     * 32 KiB of SRAM1 through the Non-secure alias at 0x20000000, while the
     * Secure monitor .data/.bss starts above that physical window. */
    WT_GTZC1_MPCBB1_SECCFGR[0] = 0x00000000u;
    WT_GTZC1_MPCBB1_SECCFGR[1] = 0x00000000u;

    /* Guests own the demo UARTs. SAU makes the APB window non-secure, but
     * STM32H5 also gates peripheral security through GTZC/TZSC. */
    WT_GTZC1_TZSC_SECCFGR1 &= ~(WT_GTZC_SECCFGR1_USART2SEC |
                                WT_GTZC_SECCFGR1_USART3SEC);

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

static void wt_clock_init(void)
{
    uint32_t reg;

    if (((WT_RCC_CFGR1 >> WT_RCC_CFGR1_SWS_SHIFT) & WT_RCC_CFGR1_SW_MASK) ==
        WT_RCC_CFGR1_SW_PLL1) {
        return;
    }

    reg = WT_PWR_VOSCR & ~WT_PWR_VOSCR_VOS_MASK;
    WT_PWR_VOSCR = reg | WT_PWR_VOSCR_SCALE0;
    while ((WT_PWR_VOSSR & WT_PWR_VOSSR_VOSRDY) == 0u) {
    }

    reg = WT_FLASH_ACR & ~(WT_FLASH_ACR_LATENCY_MASK |
                           WT_FLASH_ACR_WRHIGHFREQ_MASK);
    WT_FLASH_ACR = reg | WT_FLASH_LATENCY_5WS | WT_FLASH_WRHIGHFREQ_2;
    while ((WT_FLASH_ACR & (WT_FLASH_ACR_LATENCY_MASK |
                            WT_FLASH_ACR_WRHIGHFREQ_MASK)) !=
           (WT_FLASH_LATENCY_5WS | WT_FLASH_WRHIGHFREQ_2)) {
    }

    WT_RCC_CFGR1 = (WT_RCC_CFGR1 & ~WT_RCC_CFGR1_SW_MASK) |
                   WT_RCC_CFGR1_SW_HSI;
    while (((WT_RCC_CFGR1 >> WT_RCC_CFGR1_SWS_SHIFT) &
            WT_RCC_CFGR1_SW_MASK) != WT_RCC_CFGR1_SW_HSI) {
    }

    WT_RCC_CR &= ~WT_RCC_CR_PLL1ON;
    while ((WT_RCC_CR & WT_RCC_CR_PLL1RDY) != 0u) {
    }

    WT_RCC_CR = (WT_RCC_CR | WT_RCC_CR_HSION | WT_RCC_CR_HSEON |
                 WT_RCC_CR_HSEBYP) & ~WT_RCC_CR_HSIDIV_MASK;
    while ((WT_RCC_CR & WT_RCC_CR_HSIRDY) == 0u) {
    }
    while ((WT_RCC_CR & WT_RCC_CR_HSERDY) == 0u) {
    }

    /* NUCLEO-H563ZI HSE is the 8 MHz ST-LINK MCO. PLL1: 8 / 2 * 120 / 2
     * gives a 240 MHz core clock. APB1/APB3 are kept at 120 MHz. */
    WT_RCC_PLL1CFGR = WT_RCC_PLL1CFGR_SRC_HSE |
                      WT_RCC_PLL1CFGR_RGE_4_8 |
                      WT_RCC_PLL1CFGR_VCO_WIDE |
                      (2u << WT_RCC_PLL1CFGR_M_SHIFT);
    WT_RCC_PLL1DIVR = ((120u - 1u) << WT_RCC_PLL1DIVR_N_SHIFT) |
                      ((2u - 1u) << WT_RCC_PLL1DIVR_P_SHIFT) |
                      ((4u - 1u) << WT_RCC_PLL1DIVR_Q_SHIFT) |
                      ((2u - 1u) << WT_RCC_PLL1DIVR_R_SHIFT);
    WT_RCC_PLL1FRACR = 0u;
    WT_RCC_PLL1CFGR |= WT_RCC_PLL1CFGR_PEN |
                       WT_RCC_PLL1CFGR_QEN |
                       WT_RCC_PLL1CFGR_REN;

    WT_RCC_CFGR2 = (WT_RCC_AHB_DIV_NONE << WT_RCC_CFGR2_HPRE_SHIFT) |
                   (WT_RCC_APB_DIV_2 << WT_RCC_CFGR2_PPRE1_SHIFT) |
                   (WT_RCC_APB_DIV_NONE << WT_RCC_CFGR2_PPRE2_SHIFT) |
                   (WT_RCC_APB_DIV_2 << WT_RCC_CFGR2_PPRE3_SHIFT);

    WT_RCC_CR |= WT_RCC_CR_PLL1ON;
    while ((WT_RCC_CR & WT_RCC_CR_PLL1RDY) == 0u) {
    }

    WT_RCC_CFGR1 = (WT_RCC_CFGR1 & ~WT_RCC_CFGR1_SW_MASK) |
                   WT_RCC_CFGR1_SW_PLL1;
    while (((WT_RCC_CFGR1 >> WT_RCC_CFGR1_SWS_SHIFT) &
            WT_RCC_CFGR1_SW_MASK) != WT_RCC_CFGR1_SW_PLL1) {
    }

    /* USART2/USART3 kernel clock source 0 is PCLK1. */
    WT_RCC_CCIPR1 &= ~((WT_RCC_CCIPR_USARTSEL_MASK <<
                        WT_RCC_CCIPR1_USART2SEL_SHIFT) |
                       (WT_RCC_CCIPR_USARTSEL_MASK <<
                        WT_RCC_CCIPR1_USART3SEL_SHIFT));
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

static void wt_jump_to_ns(uint32_t msp_ns, uint32_t reset_addr)
    __attribute__((naked, noreturn));

static void wt_jump_to_ns(uint32_t msp_ns __attribute__((unused)),
                          uint32_t reset_addr __attribute__((unused)))
{
    __asm volatile(
        "msr msp_ns, r0     \n"
        "bics r1, r1, #1    \n"
        "movs r2, #0        \n"
        "msr control_ns, r2 \n"
        "isb 0xF            \n"
        "bxns r1            \n"
    );
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
    __attribute__((used));
static void wt_secure_fault_dispatch(const wt_trap_frame_t* frame)
    __attribute__((noreturn, used));

static void wt_secure_systick_dispatch(const wt_trap_frame_t* frame)
{
    wt_update_virtual_time();
    wt_monitor_on_secure_timer(frame);
}

static void wt_secure_fault_dispatch(const wt_trap_frame_t* frame)
{
    g_last_fault_address = WT_SAU_SFAR;
    wt_monitor_on_guest_fault(frame, WT_FAULT_SECURE_ESCALATION);
    wt_platform_panic();
    __builtin_unreachable();
}

static void wt_configure_uart_gpio_pin(uintptr_t gpio_base, uint32_t pin,
                                       uint32_t af)
{
    volatile uint32_t* afr;
    uint32_t shift;

    WT_GPIO_MODER(gpio_base) =
        (WT_GPIO_MODER(gpio_base) & ~(0x3u << (pin * 2u))) |
        (0x2u << (pin * 2u));
    WT_GPIO_OTYPER(gpio_base) &= ~(1u << pin);
    WT_GPIO_OSPEEDR(gpio_base) |= (0x3u << (pin * 2u));
    WT_GPIO_PUPDR(gpio_base) =
        (WT_GPIO_PUPDR(gpio_base) & ~(0x3u << (pin * 2u))) |
        (0x1u << (pin * 2u));
    WT_GPIO_SECCFGR(gpio_base) &= ~(1u << pin);

    if (pin < 8u) {
        afr = &WT_GPIO_AFRL(gpio_base);
        shift = pin * 4u;
    } else {
        afr = &WT_GPIO_AFRH(gpio_base);
        shift = (pin - 8u) * 4u;
    }

    *afr = (*afr & ~(0xFu << shift)) | ((af & 0xFu) << shift);
}

static void wt_uart_gpio_init(void)
{
    static const whal_Stm32h5_Rcc_PeriphClk gpio_clocks[] = {
        {WHAL_STM32H563_GPIOA_CLOCK},
        {WHAL_STM32H563_GPIOD_CLOCK},
    };

    for (size_t i = 0u; i < sizeof(gpio_clocks) / sizeof(gpio_clocks[0]); ++i) {
        wt_rcc_enable_clock(WT_RCC_BASE_S, &gpio_clocks[i]);
    }
    (void)WT_RCC_AHB2ENR;
    WT_PWR_CR2 |= WT_PWR_CR2_IOSV;

    /* USART2 on PA2/PA3, USART3 VCP on PD8/PD9. */
    wt_configure_uart_gpio_pin(WT_GPIOA_BASE_S, 2u, 7u);
    wt_configure_uart_gpio_pin(WT_GPIOA_BASE_S, 3u, 7u);
    wt_configure_uart_gpio_pin(WT_GPIOD_BASE_S, 8u, 7u);
    wt_configure_uart_gpio_pin(WT_GPIOD_BASE_S, 9u, 7u);
}

void wt_platform_init(void)
{
    static const whal_Stm32h5_Rcc_PeriphClk uart_clocks[] = {
        {WHAL_STM32H563_USART2_CLOCK},
        {WHAL_STM32H563_USART3_CLOCK},
    };

    wt_clock_init();
    WT_SCB_VTOR_S = WT_FLASH_S_BASE;
    wt_gtzc_init();
    wt_sau_init();
    /* Enable USART2/USART3 clocks in both security views before guests run. */
    for (size_t i = 0u; i < sizeof(uart_clocks) / sizeof(uart_clocks[0]); ++i) {
        wt_rcc_enable_clock(WT_RCC_BASE_S, &uart_clocks[i]);
        wt_rcc_enable_clock(WT_RCC_BASE_NS, &uart_clocks[i]);
    }
    wt_uart_gpio_init();
    wt_platform_zero_guest_memory(WT_GUEST0_RAM_BASE, WT_GUEST_RAM_SIZE);
    wt_platform_zero_guest_memory(WT_GUEST1_RAM_BASE, WT_GUEST_RAM_SIZE);
    g_switch_count = 0u;
    g_virtual_ms = 0u;
    g_timeslice_ms = 0u;
}

/* WolfTrust_Yield uses the cmse_nonsecure_entry attribute (rather than the
 * older naked-sg-tail-call pattern) so the compiler generates an
 * __acle_se_WolfTrust_Yield wrapper that clears scratch registers
 * (r1-r3, r12, CPSR_fs) before BXNS. Without this, values left in r1/r2
 * by wt_co_tick → wt_co_arch_switch (notably a secure-RAM coroutine
 * SP) would leak to the non-secure caller (Wave 4C audit finding). */
void WolfTrust_Yield_Impl(void);

__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
void WolfTrust_Yield(void)
{
    WolfTrust_Yield_Impl();
}

#ifdef WT_HSM_DEMO
void WolfTrust_Yield_Impl(void)
{
    /* No need to precheck g_active_guest — yield is harmless even if
     * the caller is in an invalid state; we just tick coroutines and
     * return. The next SysTick will steal CPU naturally. */
    (void)wt_co_tick(WT_HSM_YIELD_BUDGET);
}
#else
void WolfTrust_Yield_Impl(void)
{
    __asm volatile("bkpt #0x70");
    wt_platform_panic();
}
#endif

void wt_platform_start_secure_timer(uint32_t timeslice_ms)
{
    uint32_t reload;

    if (timeslice_ms == 0u) {
        wt_platform_panic();
    }

    g_timeslice_ms = timeslice_ms;
    reload = timeslice_ms * (WT_STM32H563_CORE_CLOCK_HZ / 1000u);
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
        if (wt_read_ipsr() == 0u) {
            wt_arm_secure_timer();
            wt_jump_to_ns((uint32_t)context->msp_ns, (uint32_t)context->pc);
        } else {
            wt_exception_frame_t* stacked;

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
#ifdef WT_HSM_DEMO
    /* Bring up the secure-side wolfHSM service before dispatching guests:
     *  1. coroutine scheduler (provides the bootstrap context)
     *  2. shared wolfCrypt + NVM + lock
     *  3. one transport + server context + coroutine per guest
     * Any failure here is fatal — the demo cannot proceed. */
    wt_co_init();
    if (wt_hsm_init() != 0) wt_platform_panic();
    for (wt_guest_id_t gid = 0u; gid < WT_MAX_GUESTS; gid++) {
        const wt_guest_config_t *configs;
        size_t cfg_count;
        wt_cmse_transport_cfg_t tx_cfg;
        wt_cmse_transport_ctx_t *tx_ctx;
        configs = wt_partitions_config_table(&cfg_count);
        if (configs == NULL || gid >= cfg_count) break;
        if (configs[gid].hsm_transport.size == 0u) continue;
        wt_cmse_transport_cfg_for(gid, &tx_cfg);
        tx_ctx = wt_cmse_transport_ctx_for(gid);
        if (tx_ctx == NULL) continue;
        if (wt_hsm_guest_init(gid, &wt_cmse_transport_cb, tx_ctx, &tx_cfg) != 0) {
            wt_platform_panic();
        }
    }
#endif
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

#ifdef WT_HSM_DEMO

/* Common preamble: validate the current guest is known and HSM-ready. */
static int wt_hsm_veneer_precheck(void)
{
    if (g_active_guest >= WT_MAX_GUESTS) return WH_ERROR_BADARGS;
    if (!wt_hsm_guest_ready(g_active_guest)) return WH_ERROR_BADARGS;
    return WH_ERROR_OK;
}

int WolfTrust_HSM_Submit_Impl(uint16_t size);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_HSM_Submit(uint16_t size)
{
    return WolfTrust_HSM_Submit_Impl(size);
}

int WolfTrust_HSM_Submit_Impl(uint16_t size)
{
    int rc = wt_hsm_veneer_precheck();
    if (rc != WH_ERROR_OK) return rc;
    if (size == 0u || size > WOLFHSM_CFG_COMM_DATA_LEN) return WH_ERROR_BADARGS;

    /* The transport's Recv callback walks the request slot using
     * wt_cmse_check_ns_rw, so we don't need to copy here. We just
     * need to give the coroutine some CPU so it can pick the request
     * up and process it. The size argument is informational — we do
     * not trust it for memory access, only for early validation. */
    wt_co_t *co = wt_hsm_guest_coroutine(g_active_guest);
    if (co != NULL) wt_co_wake(co);
    (void)wt_co_tick(WT_HSM_SUBMIT_BUDGET);
    return WH_ERROR_OK;
}

int WolfTrust_HSM_Poll_Impl(uint16_t seq);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_HSM_Poll(uint16_t seq)
{
    return WolfTrust_HSM_Poll_Impl(seq);
}

int WolfTrust_HSM_Poll_Impl(uint16_t seq)
{
    int rc = wt_hsm_veneer_precheck();
    wt_co_t *co;

    if (rc != WH_ERROR_OK) return rc;

    /* Poll is part of the active request/response handshake. Wake only the
     * current guest's HSM server so foreign guest buffers are never touched
     * while this guest's NS MPU window is active. */
    co = wt_hsm_guest_coroutine(g_active_guest);
    if (co != NULL) wt_co_wake(co);
    (void)wt_co_tick(WT_HSM_SUBMIT_BUDGET);

    /* Read the response notify counter from the guest's NS buffer
     * via the transport context (which already validated the pointer
     * at init). We do NOT trust `seq` for memory access — it is
     * compared against a value we read ourselves. */
    wt_cmse_transport_ctx_t *tx = wt_cmse_transport_ctx_for(g_active_guest);
    if (tx == NULL || tx->resp_csr == NULL) return WH_ERROR_NOTREADY;
    if (!wt_cmse_check_ns_rw(tx->resp_csr, sizeof(*tx->resp_csr))) {
        return WH_ERROR_ABORTED;
    }
    /* Compare notify field with the seq we were given. The client uses
     * seq as a sequence-number lookup; mismatch means "response not yet
     * ready" (or "response is for a different request, retry"). */
    if (tx->resp_csr->s.notify == seq) {
        return WH_ERROR_OK;
    }
    return WH_ERROR_NOTREADY;
}

int WolfTrust_HSM_Cancel_Impl(uint16_t seq);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_HSM_Cancel(uint16_t seq)
{
    return WolfTrust_HSM_Cancel_Impl(seq);
}

int WolfTrust_HSM_Cancel_Impl(uint16_t seq)
{
    int rc = wt_hsm_veneer_precheck();
    if (rc != WH_ERROR_OK) return rc;
    (void)seq;
    /* TODO(future): tag the per-guest server with a cancel flag the
     * coroutine inspects between message-handler steps. For now,
     * cancel is a soft no-op — the request will run to completion
     * and the client can discard the response. */
    return WH_ERROR_OK;
}

#endif /* WT_HSM_DEMO */

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
