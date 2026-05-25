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

#ifdef WT_ENGINE_HSM
#include "wolftrust/services/hsm.h"
#include "wolftrust/arch/armv8m/cmse.h"
#include "wolftrust/arch/armv8m/cmse_transport.h"
#include "wolftrust/sched/coroutine.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_transport_mem.h"

#define WT_HSM_YIELD_BUDGET  16u
#define WT_HSM_SUBMIT_BUDGET  8u
#endif /* WT_ENGINE_HSM */

/* Secure MPU attribute encodings. MPU_RBAR[2:1] = AP (access permissions),
 * MPU_RBAR[0]  = XN (execute-never), MPU_RLAR[3:1] = AttrIndx (into MAIR). */
#define WT_MPU_RBAR_XN       (1u << 0)
#define WT_MPU_RBAR_AP_RW    (0u << 1)   /* privileged RW, no access from unpriv */
#define WT_MPU_RBAR_AP_RWRW  (1u << 1)   /* RW from any priv level */
#define WT_MPU_RBAR_AP_RO    (2u << 1)   /* privileged RO, no access from unpriv */
#define WT_MPU_RBAR_AP_RORO  (3u << 1)   /* RO from any priv level */
#define WT_MPU_RBAR_SH_INNER (3u << 3)

#define WT_MPU_RLAR_EN       (1u << 0)
#define WT_MPU_RLAR_ATTRIDX_NORMAL  (0u << 1)  /* MAIR[0] = normal memory */
#define WT_MPU_RLAR_ATTRIDX_DEVICE  (1u << 1)  /* MAIR[1] = device memory */

/* MAIR encodings: normal write-back/RA/WA inner+outer = 0xFF; device-nGnRE = 0x04. */
#define WT_MPU_MAIR0_NORMAL_AT_0   0x000000FFu
#define WT_MPU_MAIR0_DEVICE_AT_1   0x00000400u

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
static uint64_t g_secure_wall_cycles;
static uintptr_t g_secure_entry_sp __attribute__((used));
static volatile uint32_t g_last_fault_address;
static volatile uint32_t g_last_fault_pc;
static volatile uint32_t g_switch_count;
static volatile uint32_t g_active_guest;
static volatile uint32_t g_secure_service_depth;

typedef struct wt_virtual_systick {
    uint32_t csr;
    uint32_t rvr;
    uint32_t cvr;
    uint64_t last_accounted_cycles;
    uint32_t owed_ticks;
    uint8_t pending;
    uint32_t accrued_ticks;
    uint32_t injected_ticks;
    uint32_t coalesced_ticks;
    uint32_t max_owed_ticks;
} wt_virtual_systick_t;

static wt_virtual_systick_t g_guest_systick[WT_MAX_GUESTS];

#ifdef WT_ENGINE_HSM
static void wt_secure_service_enter(void);
static void wt_secure_service_exit(void);
#endif

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

    /* SRAM1 MPCBB blocks are 512 B; each SECCFGR word covers 32 blocks
     * (16 KiB). The two guest windows occupy the first 64 KiB of SRAM1
     * through the Non-secure alias at 0x20000000 (32 KiB per guest after
     * the bench-driven RAM bump), while the Secure monitor .data/.bss
     * starts at 0x30028000, well above this NS region. */
    WT_GTZC1_MPCBB1_SECCFGR[0] = 0x00000000u;
    WT_GTZC1_MPCBB1_SECCFGR[1] = 0x00000000u;
    WT_GTZC1_MPCBB1_SECCFGR[2] = 0x00000000u;
    WT_GTZC1_MPCBB1_SECCFGR[3] = 0x00000000u;

    /* Guests own the UARTs. SAU makes the APB window non-secure, but
     * STM32H5 also gates peripheral security through GTZC/TZSC. */
    WT_GTZC1_TZSC_SECCFGR1 &= ~(WT_GTZC_SECCFGR1_USART2SEC |
                                WT_GTZC_SECCFGR1_USART3SEC);

    /* Crypto peripherals are secure-owned. Do not clear these bits when the
     * APB/AHB SAU windows are exposed to guests for other devices. STM32H563
     * has HASH, RNG and PKA in this GTZC register; AES/SAES are not present on
     * this line and future H5 derivatives should add their bits here. */
    WT_GTZC1_TZSC_SECCFGR3 |= (WT_GTZC_SECCFGR3_HASHSEC |
                               WT_GTZC_SECCFGR3_RNGSEC |
                               WT_GTZC_SECCFGR3_PKASEC);

    for (i = 0; i < 4u; ++i) {
        WT_GTZC1_MPCBB2_SECCFGR[i] = 0xFFFFFFFFu;
    }

    for (i = 0; i < 20u; ++i) {
        WT_GTZC1_MPCBB3_SECCFGR[i] = 0xFFFFFFFFu;
    }
}

static void wt_sau_init(void)
{
    wt_sau_set_region(0u, WT_GUEST0_FLASH_BASE,
                      WT_GUEST1_FLASH_BASE + WT_GUEST_FLASH_SIZE - 1u,
                      false);
    wt_sau_set_region(1u, WT_RAM_NS_BASE, WT_RAM_NS_BASE + 0x0009FFFFu, false);
    wt_sau_set_region(2u, WT_FLASH_NSC_BASE, WT_FLASH_NSC_END, true);
    wt_sau_set_region(3u, 0x40000000u, 0x4FFFFFFFu, false);
    WT_SAU_CTRL = 1u;
    wt_dsb();
    wt_isb();
}

/* Program one secure MPU region. base/limit are inclusive 32-byte-aligned
 * boundaries; `rbar_flags` carries XN/AP/SH, `rlar_flags` carries AttrIndx. */
static void wt_mpu_s_set_region(uint32_t rnr, uintptr_t base,
                                uintptr_t limit_inclusive,
                                uint32_t rbar_flags, uint32_t rlar_flags)
{
    WT_MPU_S_RNR  = rnr;
    WT_MPU_S_RBAR = ((uint32_t)base & 0xFFFFFFE0u) | rbar_flags;
    WT_MPU_S_RLAR = (((uint32_t)limit_inclusive & 0xFFFFFFE0u)
                    | rlar_flags | WT_MPU_RLAR_EN);
}

/* Secure-side MPU whitelist. PRIVDEFENA is OFF, so any access outside
 * the listed regions traps (MemManage / SecureFault). This catches NULL
 * pointer derefs, wild pointer writes, and stray peripheral accesses
 * from inside wolfHSM / wolfCrypt coroutines. Stack overflow is caught
 * separately via PSPLIM_S → UsageFault.STKOF. */
static void wt_mpu_s_init(void)
{
    WT_MPU_S_CTRL = 0u;
    wt_dsb();

    /* MAIR0[7:0]   = Normal WB/RA/WA  (AttrIndx 0)
     * MAIR0[15:8]  = Device nGnRE     (AttrIndx 1) */
    WT_MPU_S_MAIR0 = WT_MPU_MAIR0_NORMAL_AT_0 | WT_MPU_MAIR0_DEVICE_AT_1;
    WT_MPU_S_MAIR1 = 0u;

    /* Region 0: secure flash bank 1 RX (image, NSC stubs, .text).
     * The secure linker caps the image at 0x0C020000. */
    wt_mpu_s_set_region(0u,
        0x0C000000u, 0x0C01FFFFu,
        WT_MPU_RBAR_AP_RO | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NORMAL);

    /* Region 1: secure flash bank 2 RW-NX. The wolfHSM NVM partition
     * lives at 0x0C1FC000..0x0C1FFFFF and STM32H5 flash programming
     * writes data words directly to the destination flash address with
     * FLASH_CR.PG set (the FLASH controller intercepts the stores).
     * The peripheral's own LOCK / PG gating is the real write barrier;
     * MPU just needs to permit the addressed stores. */
    wt_mpu_s_set_region(1u,
        0x0C100000u, 0x0C1FFFFFu,
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RW | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NORMAL);

    /* Region 2: secure RAM RW-NX (.data/.bss/MSP_S + coroutine stacks). */
    wt_mpu_s_set_region(2u,
        WT_RAM_S_BASE, WT_RAM_S_BASE + WT_RAM_S_SIZE - 1u,
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RW | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NORMAL);

    /* Region 3: NS RAM RW-NX. Secure code touches this through the
     * 0x20000000 alias to exchange HSM transport buffers with guests
     * and to write fault-response CSRs. */
    wt_mpu_s_set_region(3u,
        WT_RAM_NS_BASE, WT_RAM_NS_BASE + 0x0001FFFFu,
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RW | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NORMAL);

    /* Region 4: NS flash R (so secure side can read guest image
     * metadata if needed — current code does not, but the SAU window
     * exists and we keep it consistent). XN to prevent stray Secure
     * execution into NS code. */
    wt_mpu_s_set_region(4u,
        WT_FLASH_NS_BASE, WT_FLASH_NS_BASE + 0x001FFFFFu,
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RO | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NORMAL);

    /* Region 5: SoC peripheral aperture (RCC, GTZC, GPIO, USART, FLASH
     * controller, RNG, etc.) — both the 0x40000000 NS alias and the
     * 0x50000000 secure alias fall in one 256 MiB block. */
    wt_mpu_s_set_region(5u,
        0x40000000u, 0x5FFFFFFFu,
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RW,
        WT_MPU_RLAR_ATTRIDX_DEVICE);

    /* Region 6: Cortex private peripheral bus (SCB, NVIC, SAU, MPU,
     * SysTick — everything in the 0xE0000000..0xE00FFFFF window). */
    wt_mpu_s_set_region(6u,
        0xE0000000u, 0xE00FFFFFu,
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RW,
        WT_MPU_RLAR_ATTRIDX_DEVICE);

    /* Enable: PRIVDEFENA=0 (no implicit background region), HFNMIENA=1
     * so MPU stays active during HardFault/NMI (matches what we want
     * since our MemManage handler relies on the same region table). */
    wt_dsb();
    WT_MPU_S_CTRL = WT_MPU_CTRL_HFNMIENA | WT_MPU_CTRL_ENABLE;
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
    WT_RCC_CR |= WT_RCC_CR_HSI48ON;
    while ((WT_RCC_CR & WT_RCC_CR_HSI48RDY) == 0u) {
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

static void wt_secure_systick_dispatch(const wt_trap_frame_t* frame)
    __attribute__((used));
static void wt_secure_fault_dispatch(const wt_trap_frame_t* frame)
    __attribute__((noreturn, used));

static void wt_secure_systick_dispatch(const wt_trap_frame_t* frame)
{
    g_secure_wall_cycles += g_systick_reload;
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
    static const whal_Stm32h5_Rcc_PeriphClk rng_clock =
        {WHAL_STM32H563_RNG_CLOCK};

    wt_clock_init();
    WT_SCB_VTOR_S = WT_FLASH_S_BASE;
    wt_gtzc_init();
    wt_sau_init();
    wt_mpu_s_init();
    /* Route MemManage and UsageFault to their own handlers (otherwise
     * they escalate to HardFault and we lose the fault-status registers
     * by the time we get the trap). STKOF on PSPLIM_S overflow surfaces
     * as a UsageFault. */
    WT_SCB_SHCSR_S |= WT_SCB_SHCSR_MEMFAULTENA | WT_SCB_SHCSR_USGFAULTENA;
    /* Enable USART2/USART3 clocks in both security views before guests run. */
    for (size_t i = 0u; i < sizeof(uart_clocks) / sizeof(uart_clocks[0]); ++i) {
        wt_rcc_enable_clock(WT_RCC_BASE_S, &uart_clocks[i]);
        wt_rcc_enable_clock(WT_RCC_BASE_NS, &uart_clocks[i]);
    }
    wt_rcc_enable_clock(WT_RCC_BASE_S, &rng_clock);
    wt_uart_gpio_init();
    wt_platform_zero_guest_memory(WT_GUEST0_RAM_BASE, WT_GUEST_RAM_SIZE);
    wt_platform_zero_guest_memory(WT_GUEST1_RAM_BASE, WT_GUEST_RAM_SIZE);
    g_switch_count = 0u;
    g_active_guest = UINT32_MAX;
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

#ifdef WT_ENGINE_HSM
void WolfTrust_Yield_Impl(void)
{
    wt_secure_service_enter();
    /* No need to precheck g_active_guest — yield is harmless even if
     * the caller is in an invalid state; we just tick coroutines and
     * return. The next SysTick will steal CPU naturally. */
    (void)wt_co_tick(WT_HSM_YIELD_BUDGET);
    wt_secure_service_exit();
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

static uint32_t wt_virtual_systick_period(const wt_virtual_systick_t* systick)
{
    return (systick->rvr & 0x00FFFFFFu) + 1u;
}

static bool wt_virtual_systick_active(const wt_virtual_systick_t* systick)
{
    return (systick->csr & WT_SYST_CSR_ENABLE) != 0u;
}

static bool wt_virtual_systick_irq_enabled(const wt_virtual_systick_t* systick)
{
    return (systick->csr & (WT_SYST_CSR_ENABLE | WT_SYST_CSR_TICKINT)) ==
           (WT_SYST_CSR_ENABLE | WT_SYST_CSR_TICKINT);
}

static void wt_virtual_systick_note_consumed(wt_virtual_systick_t* systick,
                                             bool hw_pending)
{
    if (systick->pending && !hw_pending) {
        if (systick->owed_ticks > 0u) {
            systick->owed_ticks--;
        }
        systick->pending = 0u;
    }
}

static void wt_virtual_systick_save_departing(void)
{
    wt_virtual_systick_t* systick;
    uint32_t csr;
    bool hw_pending;

    if (g_active_guest >= WT_MAX_GUESTS) {
        WT_SYST_NS_CSR = 0u;
        WT_SCB_ICSR_NS = WT_SCB_ICSR_PENDSTCLR;
        return;
    }

    systick = &g_guest_systick[g_active_guest];
    csr = WT_SYST_NS_CSR;
    hw_pending = (WT_SCB_ICSR_NS & WT_SCB_ICSR_PENDSTSET) != 0u;

    wt_virtual_systick_note_consumed(systick, hw_pending);
    systick->csr = csr;
    systick->rvr = WT_SYST_NS_RVR;
    systick->cvr = WT_SYST_NS_CVR;
    systick->last_accounted_cycles = g_secure_wall_cycles;

    if (!systick->pending && hw_pending && wt_virtual_systick_irq_enabled(systick)) {
        systick->owed_ticks++;
        systick->pending = 1u;
        systick->accrued_ticks++;
        if (systick->owed_ticks > systick->max_owed_ticks) {
            systick->max_owed_ticks = systick->owed_ticks;
        }
    }

    WT_SYST_NS_CSR = 0u;
    WT_SCB_ICSR_NS = WT_SCB_ICSR_PENDSTCLR;
}

static void wt_virtual_systick_account_elapsed(wt_virtual_systick_t* systick)
{
    uint64_t elapsed;
    uint32_t period;
    uint32_t remaining;
    uint64_t ticks;
    uint64_t rem;

    if (systick->last_accounted_cycles == 0u) {
        systick->last_accounted_cycles = g_secure_wall_cycles;
        return;
    }

    elapsed = g_secure_wall_cycles - systick->last_accounted_cycles;
    systick->last_accounted_cycles = g_secure_wall_cycles;
    if (!wt_virtual_systick_active(systick) || elapsed == 0u) {
        return;
    }

    period = wt_virtual_systick_period(systick);
    remaining = (systick->cvr == 0u || systick->cvr >= period) ? period :
                (systick->cvr + 1u);

    if (elapsed < remaining) {
        systick->cvr = remaining - (uint32_t)elapsed - 1u;
        return;
    }

    elapsed -= remaining;
    ticks = 1u + (elapsed / period);
    rem = elapsed % period;
    systick->cvr = (uint32_t)(period - rem - 1u);

    if (ticks > (uint64_t)(UINT32_MAX - systick->owed_ticks)) {
        systick->owed_ticks = UINT32_MAX;
    }
    else {
        systick->owed_ticks += (uint32_t)ticks;
    }

    if (ticks > (uint64_t)(UINT32_MAX - systick->accrued_ticks)) {
        systick->accrued_ticks = UINT32_MAX;
    }
    else {
        systick->accrued_ticks += (uint32_t)ticks;
    }

    if (systick->owed_ticks > systick->max_owed_ticks) {
        systick->max_owed_ticks = systick->owed_ticks;
    }
}

static void wt_virtual_systick_restore_arriving(wt_guest_id_t guest_id)
{
    wt_virtual_systick_t* systick;

    WT_SCB_ICSR_NS = WT_SCB_ICSR_PENDSTCLR;
    if (guest_id >= WT_MAX_GUESTS) {
        return;
    }

    systick = &g_guest_systick[guest_id];
    wt_virtual_systick_account_elapsed(systick);

    WT_SYST_NS_CSR = 0u;
    WT_SYST_NS_RVR = systick->rvr;
    WT_SYST_NS_CVR = 0u;
    if (wt_virtual_systick_active(systick)) {
        WT_SYST_NS_CSR = systick->csr & ~WT_SYST_CSR_COUNTFLAG;
    }

    if (systick->owed_ticks > 0u && wt_virtual_systick_irq_enabled(systick)) {
        if (!systick->pending) {
            WT_SCB_ICSR_NS = WT_SCB_ICSR_PENDSTSET;
            systick->pending = 1u;
            systick->injected_ticks++;
        }
        else {
            systick->coalesced_ticks++;
            WT_SCB_ICSR_NS = WT_SCB_ICSR_PENDSTSET;
        }
    }
}

static void wt_virtual_systick_reset(wt_guest_id_t guest_id)
{
    wt_virtual_systick_t* systick;

    if (guest_id >= WT_MAX_GUESTS) {
        return;
    }
    systick = &g_guest_systick[guest_id];
    systick->csr = 0u;
    systick->rvr = 0u;
    systick->cvr = 0u;
    systick->last_accounted_cycles = g_secure_wall_cycles;
    systick->owed_ticks = 0u;
    systick->pending = 0u;
    systick->accrued_ticks = 0u;
    systick->injected_ticks = 0u;
    systick->coalesced_ticks = 0u;
    systick->max_owed_ticks = 0u;
}

void wt_platform_prepare_guest_return(wt_guest_id_t guest_id,
                                      const wt_guest_context_t* context)
{
    if (context == NULL) {
        return;
    }

    wt_virtual_systick_save_departing();

    WT_SCB_VTOR_NS = (uint32_t)context->vector_table_ns;
    g_active_guest = guest_id;
    wt_virtual_systick_restore_arriving(guest_id);
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
    if (base == WT_GUEST0_RAM_BASE && size >= WT_GUEST_RAM_SIZE) {
        wt_virtual_systick_reset(0u);
    }
    else if (base == WT_GUEST1_RAM_BASE && size >= WT_GUEST_RAM_SIZE) {
        wt_virtual_systick_reset(1u);
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
#ifdef WT_ENGINE_HSM
    /* Bring up the secure-side wolfHSM service before dispatching guests:
     *  1. coroutine scheduler (provides the bootstrap context)
     *  2. shared wolfCrypt + NVM + lock
     *  3. one transport + server context + coroutine per guest
     * Any failure here is fatal because guests require this engine. */
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

#ifdef WT_ENGINE_HSM
/* -----------------------------------------------------------------------
 * Secure-side coroutine fault path.
 *
 * MemManage and UsageFault can fire from within a wolfHSM coroutine when:
 *   - PSPLIM_S is hit (UsageFault.STKOF) — coroutine stack overflow,
 *   - MPU_S blocks a wild read/write (MemManage IACCVIOL/DACCVIOL),
 *   - the coroutine executes an illegal instruction (UsageFault).
 *
 * Recovery model:
 *   1. C dispatcher logs the fault, identifies the running coroutine
 *      (g_co_current via wt_co_current), maps it back to a guest_id,
 *      hands the NS client a WH_ERROR_ABORTED via wt_hsm_signal_fault,
 *      drops any mutex held by the dying coroutine, and marks the
 *      coroutine WT_CO_FAULTED.
 *   2. The naked handler asm fabricates a Secure-Thread MSP exception
 *      frame on MSP_S that targets wt_co_fault_recovery_thunk, then
 *      EXC_RETURNs. Hardware lands in the thunk on MSP_S, the thunk
 *      pops the bootstrap's saved {r4-r11, lr} frame, and execution
 *      resumes inside wt_co_tick as if the coroutine had voluntarily
 *      switched back. The scheduler picks up the next runnable
 *      coroutine — the faulted one is no longer on the runqueue.
 *
 * If the fault fires while bootstrap (monitor) is running there is no
 * coroutine to abandon and no saved frame to unwind to, so the C
 * dispatcher panics.
 * ----------------------------------------------------------------------- */
static void wt_secure_coroutine_fault_dispatch(void) __attribute__((used));
static void wt_secure_coroutine_fault_dispatch(void)
{
    uint32_t cfsr = WT_SCB_CFSR_S;

    if ((cfsr & WT_SCB_CFSR_MMFSR_MMARVALID) != 0u) {
        g_last_fault_address = WT_SCB_MMFAR_S;
    }
    /* Write-1-to-clear so the next fault is observable. */
    WT_SCB_CFSR_S = cfsr;

    wt_co_t *co = wt_co_current();
    if (co == NULL) {
        /* Bootstrap took the fault — no coroutine to abandon. */
        wt_platform_panic();
    }

    wt_guest_id_t gid = wt_hsm_guest_for_coroutine(co);
    if (gid < WT_MAX_GUESTS) {
        (void)wt_hsm_signal_fault(gid);
    }

    wt_co_mark_faulted(co);
}

/* Shared tail for MemManage_Handler and UsageFault_Handler. Naked so
 * we control the stack layout the EXC_RETURN unwinds through. */
__attribute__((naked, used))
static void wt_secure_coroutine_fault_entry(void)
{
    __asm volatile(
        "bl     wt_secure_coroutine_fault_dispatch  \n"
        /* Fabricate an 8-word exception frame on MSP_S whose PC field
         * points at the recovery thunk. r0-r3, r12, lr are don't-care
         * (the thunk's first instruction is `pop {r4-r11, pc}`). xPSR
         * carries only the Thumb bit. */
        "sub    sp, sp, #32                         \n"
        "movs   r0, #0                              \n"
        "str    r0, [sp, #0]                        \n"
        "str    r0, [sp, #4]                        \n"
        "str    r0, [sp, #8]                        \n"
        "str    r0, [sp, #12]                       \n"
        "str    r0, [sp, #16]                       \n"
        "str    r0, [sp, #20]                       \n"
        "movw   r0, #:lower16:wt_co_fault_recovery_thunk \n"
        "movt   r0, #:upper16:wt_co_fault_recovery_thunk \n"
        "str    r0, [sp, #24]                       \n"
        "movw   r0, #0x0000                         \n"
        "movt   r0, #0x0100                         \n"
        "str    r0, [sp, #28]                       \n"
        /* Drop PSPLIM_S — wt_co_arch_switch reinstalls it for the next
         * coroutine. PSP_S itself is left pointing into the dead
         * coroutine's stack; harmless because CONTROL.SPSEL=0 on
         * return-to-Thread-MSP and arch_switch will overwrite PSP_S
         * before re-enabling PSP. */
        "movs   r0, #0                              \n"
        "msr    psplim, r0                          \n"
        /* EXC_RETURN = 0xFFFFFFF9: Secure Thread mode using MSP_S, no
         * FP context. mvn of 6 builds the value with no literal pool. */
        "mvn    lr, #6                              \n"
        "bx     lr                                  \n"
    );
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile("b wt_secure_coroutine_fault_entry \n");
}

__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile("b wt_secure_coroutine_fault_entry \n");
}
#endif /* WT_ENGINE_HSM */

#ifdef WT_ENGINE_HSM

static void wt_secure_service_enter(void)
{
    g_secure_service_depth++;
}

static void wt_secure_service_exit(void)
{
    if (g_secure_service_depth == 0u) {
        wt_platform_panic();
    }
    g_secure_service_depth--;
}

bool wt_platform_secure_service_active(void)
{
    return g_secure_service_depth != 0u;
}

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
    int rc;

    wt_secure_service_enter();

    rc = wt_hsm_veneer_precheck();
    if (rc != WH_ERROR_OK) goto out;
    if (size == 0u || size > WOLFHSM_CFG_COMM_DATA_LEN) {
        rc = WH_ERROR_BADARGS;
        goto out;
    }

    /* The transport's Recv callback walks the request slot using
     * wt_cmse_check_ns_rw, so we don't need to copy here. We just
     * need to give the coroutine some CPU so it can pick the request
     * up and process it. The size argument is informational — we do
     * not trust it for memory access, only for early validation. */
    wt_co_t *co = wt_hsm_guest_coroutine(g_active_guest);
    if (co != NULL) wt_co_wake(co);
    (void)wt_co_tick(WT_HSM_SUBMIT_BUDGET);
    rc = WH_ERROR_OK;

out:
    wt_secure_service_exit();
    return rc;
}

int WolfTrust_HSM_Poll_Impl(uint16_t seq);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_HSM_Poll(uint16_t seq)
{
    return WolfTrust_HSM_Poll_Impl(seq);
}

int WolfTrust_HSM_Poll_Impl(uint16_t seq)
{
    int rc;
    wt_co_t *co;
    wt_cmse_transport_ctx_t *tx;

    wt_secure_service_enter();

    rc = wt_hsm_veneer_precheck();
    if (rc != WH_ERROR_OK) goto out;

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
    tx = wt_cmse_transport_ctx_for(g_active_guest);
    if (tx == NULL || tx->resp_csr == NULL) {
        rc = WH_ERROR_NOTREADY;
        goto out;
    }
    if (!wt_cmse_check_ns_rw(tx->resp_csr, sizeof(*tx->resp_csr))) {
        rc = WH_ERROR_ABORTED;
        goto out;
    }
    /* Compare notify field with the seq we were given. The client uses
     * seq as a sequence-number lookup; mismatch means "response not yet
     * ready" (or "response is for a different request, retry"). */
    if (tx->resp_csr->s.notify == seq) {
        rc = WH_ERROR_OK;
        goto out;
    }
    rc = WH_ERROR_NOTREADY;

out:
    wt_secure_service_exit();
    return rc;
}

int WolfTrust_HSM_Cancel_Impl(uint16_t seq);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_HSM_Cancel(uint16_t seq)
{
    return WolfTrust_HSM_Cancel_Impl(seq);
}

int WolfTrust_HSM_Cancel_Impl(uint16_t seq)
{
    int rc;

    wt_secure_service_enter();

    rc = wt_hsm_veneer_precheck();
    if (rc != WH_ERROR_OK) goto out;
    (void)seq;
    /* TODO(future): tag the per-guest server with a cancel flag the
     * coroutine inspects between message-handler steps. For now,
     * cancel is a soft no-op — the request will run to completion
     * and the client can discard the response. */
    rc = WH_ERROR_OK;

out:
    wt_secure_service_exit();
    return rc;
}

#endif /* WT_ENGINE_HSM */

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
