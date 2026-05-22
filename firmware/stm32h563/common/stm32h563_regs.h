#ifndef WOLFTRUST_FW_STM32H563_REGS_H
#define WOLFTRUST_FW_STM32H563_REGS_H

#include <stdint.h>

#define WT_SCB_VTOR_S            (*(volatile uint32_t*)0xE000ED08u)
#define WT_SCB_VTOR_NS           (*(volatile uint32_t*)0xE002ED08u)
#define WT_SCB_SHCSR_S           (*(volatile uint32_t*)0xE000ED24u)
#define WT_SCB_ICSR_NS           (*(volatile uint32_t*)0xE002ED04u)
#define WT_SCB_ICSR_PENDSTCLR    (1u << 25)

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
#define WT_SAU_RNR               (*(volatile uint32_t*)0xE000EDD4u)
#define WT_SAU_RBAR              (*(volatile uint32_t*)0xE000EDD8u)
#define WT_SAU_RLAR              (*(volatile uint32_t*)0xE000EDDCu)
#define WT_SAU_SFSR              (*(volatile uint32_t*)0xE000EDE4u)
#define WT_SAU_SFAR              (*(volatile uint32_t*)0xE000EDE8u)

#define WT_MPU_NS_TYPE           (*(volatile uint32_t*)0xE002ED90u)
#define WT_MPU_NS_CTRL           (*(volatile uint32_t*)0xE002ED94u)
#define WT_MPU_NS_RNR            (*(volatile uint32_t*)0xE002ED98u)
#define WT_MPU_NS_RBAR           (*(volatile uint32_t*)0xE002ED9Cu)
#define WT_MPU_NS_RLAR           (*(volatile uint32_t*)0xE002EDA0u)
#define WT_MPU_NS_MAIR0          (*(volatile uint32_t*)0xE002EDA4u)

#define WT_RCC_BASE_S            0x54020C00u
#define WT_RCC_AHB2ENR           (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x8Cu))
#define WT_RCC_APB1LENR          (*(volatile uint32_t*)(WT_RCC_BASE_S + 0x9Cu))

#define WT_GTZC1_BASE_S          0x50032400u
#define WT_GTZC1_MPCBB1_SECCFGR  ((volatile uint32_t *)(WT_GTZC1_BASE_S + 0x0800u + 0x100u))
#define WT_GTZC1_MPCBB2_SECCFGR  ((volatile uint32_t *)(WT_GTZC1_BASE_S + 0x0C00u + 0x100u))
#define WT_GTZC1_MPCBB3_SECCFGR  ((volatile uint32_t *)(WT_GTZC1_BASE_S + 0x1000u + 0x100u))

#define WT_RCC_AHB2ENR_GTZC1EN   (1u << 22)

#define WT_SYST_CSR              (*(volatile uint32_t*)0xE000E010u)
#define WT_SYST_RVR              (*(volatile uint32_t*)0xE000E014u)
#define WT_SYST_CVR              (*(volatile uint32_t*)0xE000E018u)

#define WT_SYST_NS_CSR           (*(volatile uint32_t*)0xE002E010u)
#define WT_SYST_NS_RVR           (*(volatile uint32_t*)0xE002E014u)
#define WT_SYST_NS_CVR           (*(volatile uint32_t*)0xE002E018u)

#define WT_SYST_CSR_ENABLE       (1u << 0)
#define WT_SYST_CSR_TICKINT      (1u << 1)
#define WT_SYST_CSR_CLKSOURCE    (1u << 2)

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
