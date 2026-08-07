/* stm32h563_regs.h
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

#ifndef WOLFTRUST_FW_STM32H563_REGS_H
#define WOLFTRUST_FW_STM32H563_REGS_H

#include <stdint.h>
#include <wolfHAL/platform/st/stm32h563xx.h>

#define WT_SCB_VTOR_S            (*(volatile uint32_t*)0xE000ED08u)
#define WT_SCB_VTOR_NS           (*(volatile uint32_t*)0xE002ED08u)
#define WT_SCB_CCR_S             (*(volatile uint32_t*)0xE000ED14u)
#define WT_SCB_SHPR3_S           (*(volatile uint32_t*)0xE000ED20u)
#define WT_SCB_SHCSR_S           (*(volatile uint32_t*)0xE000ED24u)
#define WT_SCB_CFSR_S            (*(volatile uint32_t*)0xE000ED28u)
#define WT_SCB_MMFAR_S           (*(volatile uint32_t*)0xE000ED34u)
#define WT_SCB_BFAR_S            (*(volatile uint32_t*)0xE000ED38u)
#define WT_SCB_ICSR_S            (*(volatile uint32_t*)0xE000ED04u)
#define WT_SCB_ICSR_NS           (*(volatile uint32_t*)0xE002ED04u)
#define WT_SCB_ICSR_PENDSVCLR    (1u << 27)
#define WT_SCB_ICSR_PENDSVSET    (1u << 28)
#define WT_SCB_ICSR_PENDSTCLR    (1u << 25)
#define WT_SCB_ICSR_PENDSTSET    (1u << 26)

#define WT_SCB_SHPR3_PENDSV_SHIFT 16u

#define WT_SCB_SHCSR_MEMFAULTENA (1u << 16)
#define WT_SCB_SHCSR_BUSFAULTENA (1u << 17)
#define WT_SCB_SHCSR_USGFAULTENA (1u << 18)

#define WT_SCB_CFSR_MMFSR_MASK   0x000000FFu
#define WT_SCB_CFSR_BFSR_MASK    0x0000FF00u
#define WT_SCB_CFSR_UFSR_MASK    0xFFFF0000u

#define WT_SCB_CFSR_MMFSR_IACCVIOL    (1u << 0)
#define WT_SCB_CFSR_MMFSR_DACCVIOL    (1u << 1)
#define WT_SCB_CFSR_MMFSR_MUNSTKERR   (1u << 3)
#define WT_SCB_CFSR_MMFSR_MSTKERR     (1u << 4)
#define WT_SCB_CFSR_MMFSR_MLSPERR     (1u << 5)
#define WT_SCB_CFSR_MMFSR_MMARVALID   (1u << 7)

#define WT_SCB_CFSR_UFSR_STKOF        (1u << 20)  /* UFSR bit 4 lifted to CFSR bit 20 */

#define WT_MPU_S_TYPE            (*(volatile uint32_t*)0xE000ED90u)
#define WT_MPU_S_CTRL            (*(volatile uint32_t*)0xE000ED94u)
#define WT_MPU_S_RNR             (*(volatile uint32_t*)0xE000ED98u)
#define WT_MPU_S_RBAR            (*(volatile uint32_t*)0xE000ED9Cu)
#define WT_MPU_S_RLAR            (*(volatile uint32_t*)0xE000EDA0u)
#define WT_MPU_S_MAIR0           (*(volatile uint32_t*)0xE000EDC0u)
#define WT_MPU_S_MAIR1           (*(volatile uint32_t*)0xE000EDC4u)

#define WT_MPU_CTRL_ENABLE       (1u << 0)
#define WT_MPU_CTRL_HFNMIENA     (1u << 1)
#define WT_MPU_CTRL_PRIVDEFENA   (1u << 2)

#define WT_NVIC_ISER0            (*(volatile uint32_t*)0xE000E100u)
#define WT_NVIC_ISER1            (*(volatile uint32_t*)0xE000E104u)
#define WT_NVIC_ICER0            (*(volatile uint32_t*)0xE000E180u)
#define WT_NVIC_ICER1            (*(volatile uint32_t*)0xE000E184u)
#define WT_NVIC_ISPR0            (*(volatile uint32_t*)0xE000E200u)
#define WT_NVIC_ISPR1            (*(volatile uint32_t*)0xE000E204u)
#define WT_NVIC_ICPR0            (*(volatile uint32_t*)0xE000E280u)
#define WT_NVIC_ICPR1            (*(volatile uint32_t*)0xE000E284u)
#define WT_NVIC_ITNS0            (*(volatile uint32_t*)0xE000E380u)
#define WT_NVIC_ITNS1            (*(volatile uint32_t*)0xE000E384u)

#define WT_SAU_CTRL              (*(volatile uint32_t*)0xE000EDD0u)
#define WT_SAU_TYPE              (*(volatile const uint32_t*)0xE000EDD4u)
#define WT_SAU_RNR               (*(volatile uint32_t*)0xE000EDD8u)
#define WT_SAU_RBAR              (*(volatile uint32_t*)0xE000EDDCu)
#define WT_SAU_RLAR              (*(volatile uint32_t*)0xE000EDE0u)
#define WT_SAU_SFSR              (*(volatile uint32_t*)0xE000EDE4u)
#define WT_SAU_SFAR              (*(volatile uint32_t*)0xE000EDE8u)

#define WT_MPU_NS_TYPE           (*(volatile uint32_t*)0xE002ED90u)
#define WT_MPU_NS_CTRL           (*(volatile uint32_t*)0xE002ED94u)
#define WT_MPU_NS_RNR            (*(volatile uint32_t*)0xE002ED98u)
#define WT_MPU_NS_RBAR           (*(volatile uint32_t*)0xE002ED9Cu)
#define WT_MPU_NS_RLAR           (*(volatile uint32_t*)0xE002EDA0u)
#define WT_MPU_NS_MAIR0          (*(volatile uint32_t*)0xE002EDC0u)

#define WT_RCC_BASE_S            0x54020C00u
#define WT_RCC_BASE_NS           WHAL_STM32H5_RCC_BASE
#define WT_RCC_CR                (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x00u))
#define WT_RCC_CFGR1             (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x1Cu))
#define WT_RCC_CFGR2             (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x20u))
#define WT_RCC_PLL1CFGR          (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x28u))
#define WT_RCC_PLL1DIVR          (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x34u))
#define WT_RCC_PLL1FRACR         (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x38u))
#define WT_RCC_AHB1ENR           (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x88u))
#define WT_RCC_AHB2ENR           (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x8Cu))
#define WT_RCC_APB1LENR          (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x9Cu))
#define WT_RCC_CCIPR1            (*(volatile uint32_t*)(WT_RCC_BASE_S + 0xD8u))
#define WT_RCC_AHB2ENR_NS        (*(volatile uint32_t*)(WT_RCC_BASE_NS + 0x8Cu))
#define WT_RCC_APB1LENR_NS       (*(volatile uint32_t*)(WT_RCC_BASE_NS + 0x9Cu))

#define WT_RCC_CR_HSION          (1u << 0)
#define WT_RCC_CR_HSIRDY         (1u << 1)
#define WT_RCC_CR_HSIDIV_MASK    (0x3u << 3)
#define WT_RCC_CR_HSI48ON        (1u << 12)
#define WT_RCC_CR_HSI48RDY       (1u << 13)
#define WT_RCC_CR_HSEON          (1u << 16)
#define WT_RCC_CR_HSERDY         (1u << 17)
#define WT_RCC_CR_HSEBYP         (1u << 18)
#define WT_RCC_CR_PLL1ON         (1u << 24)
#define WT_RCC_CR_PLL1RDY        (1u << 25)

#define WT_RCC_CFGR1_SW_MASK     0x3u
#define WT_RCC_CFGR1_SW_HSI      0x0u
#define WT_RCC_CFGR1_SW_PLL1     0x3u
#define WT_RCC_CFGR1_SWS_SHIFT   3u
#define WT_RCC_CFGR2_HPRE_SHIFT  0u
#define WT_RCC_CFGR2_PPRE1_SHIFT 4u
#define WT_RCC_CFGR2_PPRE2_SHIFT 8u
#define WT_RCC_CFGR2_PPRE3_SHIFT 12u
#define WT_RCC_AHB_DIV_NONE      0x0u
#define WT_RCC_APB_DIV_NONE      0x0u
#define WT_RCC_APB_DIV_2         0x4u

#define WT_RCC_PLL1CFGR_SRC_HSE  0x3u
#define WT_RCC_PLL1CFGR_RGE_4_8  (0x2u << 2)
#define WT_RCC_PLL1CFGR_VCO_WIDE 0x0u
#define WT_RCC_PLL1CFGR_M_SHIFT  8u
#define WT_RCC_PLL1CFGR_PEN      (1u << 16)
#define WT_RCC_PLL1CFGR_QEN      (1u << 17)
#define WT_RCC_PLL1CFGR_REN      (1u << 18)
#define WT_RCC_PLL1DIVR_N_SHIFT  0u
#define WT_RCC_PLL1DIVR_P_SHIFT  9u
#define WT_RCC_PLL1DIVR_Q_SHIFT  16u
#define WT_RCC_PLL1DIVR_R_SHIFT  24u

#define WT_RCC_CCIPR1_USART2SEL_SHIFT 3u
#define WT_RCC_CCIPR1_USART3SEL_SHIFT 6u
#define WT_RCC_CCIPR_USARTSEL_MASK    0x7u

#define WT_FLASH_BASE_S          0x50022000u
#define WT_FLASH_ACR             (*(volatile uint32_t*)(WT_FLASH_BASE_S + 0x00u))
#define WT_FLASH_ACR_LATENCY_MASK 0xFu
#define WT_FLASH_ACR_WRHIGHFREQ_MASK (0x3u << 4)
#define WT_FLASH_LATENCY_5WS     0x5u
#define WT_FLASH_WRHIGHFREQ_2    (0x2u << 4)

#define WT_PWR_BASE_S            0x54020800u
#define WT_PWR_CR2               (*(volatile uint32_t*)(WT_PWR_BASE_S + 0x04u))
#define WT_PWR_VOSCR             (*(volatile uint32_t*)(WT_PWR_BASE_S + 0x10u))
#define WT_PWR_VOSSR             (*(volatile uint32_t*)(WT_PWR_BASE_S + 0x14u))
#define WT_PWR_CR2_IOSV          (1u << 9)
#define WT_PWR_VOSCR_VOS_MASK    (0x3u << 4)
#define WT_PWR_VOSCR_SCALE0      (0x3u << 4)
#define WT_PWR_VOSSR_VOSRDY      (1u << 3)

#define WT_GPIOA_BASE_S          (WHAL_STM32H563_GPIO_BASE + 0x10000000u)
#define WT_GPIOD_BASE_S          (WT_GPIOA_BASE_S + 0x0C00u)
#define WT_GPIO_MODER(base)      (*(volatile uint32_t*)((base) + 0x00u))
#define WT_GPIO_OTYPER(base)     (*(volatile uint32_t*)((base) + 0x04u))
#define WT_GPIO_OSPEEDR(base)    (*(volatile uint32_t*)((base) + 0x08u))
#define WT_GPIO_PUPDR(base)      (*(volatile uint32_t*)((base) + 0x0Cu))
#define WT_GPIO_AFRL(base)       (*(volatile uint32_t*)((base) + 0x20u))
#define WT_GPIO_AFRH(base)       (*(volatile uint32_t*)((base) + 0x24u))
#define WT_GPIO_SECCFGR(base)    (*(volatile uint32_t*)((base) + 0x30u))

#define WT_GTZC1_BASE_S          0x50032400u
#define WT_GTZC1_TZSC_SECCFGR1   (*(volatile uint32_t *)(WT_GTZC1_BASE_S + 0x10u))
#define WT_GTZC1_TZSC_SECCFGR3   (*(volatile uint32_t *)(WT_GTZC1_BASE_S + 0x18u))
#define WT_GTZC1_MPCBB1_SECCFGR  ((volatile uint32_t *)(WT_GTZC1_BASE_S + 0x0800u + 0x100u))
#define WT_GTZC1_MPCBB2_SECCFGR  ((volatile uint32_t *)(WT_GTZC1_BASE_S + 0x0C00u + 0x100u))
#define WT_GTZC1_MPCBB3_SECCFGR  ((volatile uint32_t *)(WT_GTZC1_BASE_S + 0x1000u + 0x100u))

#define WT_RCC_AHB1ENR_GTZC1EN   (1u << 24)
#define WT_RCC_AHB2ENR_GPIOAEN   (1u << 0)
#define WT_RCC_AHB2ENR_GPIODEN   (1u << 3)
#define WT_GTZC_SECCFGR1_USART2SEC (1u << 13)
#define WT_GTZC_SECCFGR1_USART3SEC (1u << 14)
#define WT_GTZC_SECCFGR3_HASHSEC   (1u << 17)
#define WT_GTZC_SECCFGR3_RNGSEC    (1u << 18)
#define WT_GTZC_SECCFGR3_PKASEC    (1u << 20)

#define WT_STM32H563_CORE_CLOCK_HZ 240000000u
#define WT_STM32H563_APB1_CLOCK_HZ 120000000u

#define WT_SYST_CSR              (*(volatile uint32_t*)0xE000E010u)
#define WT_SYST_RVR              (*(volatile uint32_t*)0xE000E014u)
#define WT_SYST_CVR              (*(volatile uint32_t*)0xE000E018u)

#define WT_SYST_NS_CSR           (*(volatile uint32_t*)0xE002E010u)
#define WT_SYST_NS_RVR           (*(volatile uint32_t*)0xE002E014u)
#define WT_SYST_NS_CVR           (*(volatile uint32_t*)0xE002E018u)

#define WT_SYST_CSR_ENABLE       (1u << 0)
#define WT_SYST_CSR_TICKINT      (1u << 1)
#define WT_SYST_CSR_CLKSOURCE    (1u << 2)
#define WT_SYST_CSR_COUNTFLAG    (1u << 16)

static inline void wt_dsb(void)
{
    __asm volatile("dsb 0xF" ::: "memory");
}

static inline void wt_isb(void)
{
    __asm volatile("isb 0xF" ::: "memory");
}

static inline uint32_t wt_thumb_insn_len(uint32_t pc)
{
    uint16_t hw = *(uint16_t*)pc;

    if ((hw & 0xF800u) == 0xE800u || (hw & 0xF800u) == 0xF000u ||
        (hw & 0xF800u) == 0xF800u) {
        return 4u;
    }

    return 2u;
}

#endif
