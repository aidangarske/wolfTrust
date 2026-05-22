/* guest.c
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

#include <stdint.h>

#ifndef WT_GUEST_ID
#define WT_GUEST_ID 0u
#endif

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

#define USART2_BASE           0x40004400u
#define USART3_BASE           0x40004800u
#define USART_CR1(base)       (*(volatile uint32_t *)((base) + 0x00u))
#define USART_CR2(base)       (*(volatile uint32_t *)((base) + 0x04u))
#define USART_CR3(base)       (*(volatile uint32_t *)((base) + 0x08u))
#define USART_BRR(base)       (*(volatile uint32_t *)((base) + 0x0Cu))
#define USART_ISR(base)       (*(volatile uint32_t *)((base) + 0x1Cu))
#define USART_TDR(base)       (*(volatile uint32_t *)((base) + 0x28u))

#define USART_CR1_UE          (1u << 0)
#define USART_CR1_TE          (1u << 3)
#define USART_ISR_TXE         (1u << 7)

#define WT_SYSCLK_HZ          64000000u

#define SYST_CSR     (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR     (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR     (*(volatile uint32_t *)0xE000E018u)
#define SYST_CSR_CLKSOURCE   (1u << 2)
#define SYST_CSR_TICKINT     (1u << 1)
#define SYST_CSR_ENABLE      (1u << 0)

typedef struct wt_guest_mailbox {
    volatile uint32_t boot_count;
    volatile uint32_t heartbeat;
    volatile uint32_t signature;
    volatile uint32_t virtual_ms;
    volatile uint32_t lines_printed;
    volatile uint32_t next_print_ms;
    volatile uint32_t run_token;
} wt_guest_mailbox_t;

static wt_guest_mailbox_t g_mailbox __attribute__((section(".shared")));
static volatile uint32_t g_tick_ms;
static uint32_t g_next_print_ms;

static void default_handler(void)
{
    for (;;) {
    }
}

void Reset_Handler(void);
void NMI_Handler(void) __attribute__((weak, alias("default_handler")));
void HardFault_Handler(void) __attribute__((weak, alias("default_handler")));
void MemManage_Handler(void) __attribute__((weak, alias("default_handler")));
void BusFault_Handler(void) __attribute__((weak, alias("default_handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("default_handler")));
void SVC_Handler(void) __attribute__((weak, alias("default_handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("default_handler")));
void PendSV_Handler(void) __attribute__((weak, alias("default_handler")));
void SysTick_Handler(void);

__attribute__((section(".vectors")))
const uint32_t g_vectors[16] = {
    [0] = (uint32_t)&_estack,
    [1] = (uint32_t)&Reset_Handler,
    [2] = (uint32_t)&NMI_Handler,
    [3] = (uint32_t)&HardFault_Handler,
    [4] = (uint32_t)&MemManage_Handler,
    [5] = (uint32_t)&BusFault_Handler,
    [6] = (uint32_t)&UsageFault_Handler,
    [11] = (uint32_t)&SVC_Handler,
    [12] = (uint32_t)&DebugMon_Handler,
    [14] = (uint32_t)&PendSV_Handler,
    [15] = (uint32_t)&SysTick_Handler
};

static uintptr_t wt_uart_base(void)
{
#if WT_GUEST_ID == 0u
    return USART2_BASE;
#else
    return USART3_BASE;
#endif
}

static void wt_copy_data(void)
{
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;

    while (dst < &_edata) {
        *dst++ = *src++;
    }
}

static void wt_zero_bss(void)
{
    uint32_t* dst = &_sbss;

    while (dst < &_ebss) {
        *dst++ = 0u;
    }
}

static void wt_uart_init(void)
{
    uintptr_t base = wt_uart_base();
    uint32_t brr = WT_SYSCLK_HZ / 115200u;

    USART_CR1(base) = 0u;
    USART_CR2(base) = 0u;
    USART_CR3(base) = 0u;
    USART_BRR(base) = brr;
    USART_CR1(base) = USART_CR1_UE | USART_CR1_TE;
}

static void wt_uart_putc(char c)
{
    uintptr_t base = wt_uart_base();

    while ((USART_ISR(base) & USART_ISR_TXE) == 0u) {
    }

    USART_TDR(base) = (uint32_t)(uint8_t)c;
}

static void wt_uart_put_u32(uint32_t value)
{
    char buf[10];
    uint32_t i = 0u;

    if (value == 0u) {
        wt_uart_putc('0');
        return;
    }

    while (value != 0u) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0u) {
        wt_uart_putc(buf[--i]);
    }
}

static void wt_print_status(uint32_t second_mark)
{
    wt_uart_putc('g');
    wt_uart_put_u32(WT_GUEST_ID);
    wt_uart_putc(':');
    wt_uart_put_u32(second_mark);
    wt_uart_putc('\n');
}

static void wt_systick_init(void)
{
    SYST_CSR = 0u;
    SYST_RVR = (WT_SYSCLK_HZ / 1000u) - 1u;
    SYST_CVR = 0u;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

void SysTick_Handler(void)
{
    g_tick_ms++;
    g_mailbox.heartbeat++;
    if (g_tick_ms >= g_next_print_ms) {
        wt_print_status(g_next_print_ms / 1000u);
        g_mailbox.lines_printed++;
        g_next_print_ms += 1000u;
    }
}

void Reset_Handler(void)
{
    if (g_mailbox.boot_count == 0u) {
        wt_copy_data();
        wt_zero_bss();
        wt_uart_init();

        g_mailbox.boot_count = 1u;
        g_mailbox.signature = 0x47554530u + WT_GUEST_ID;
        g_mailbox.heartbeat = 0u;
        g_mailbox.virtual_ms = 0u;
        g_mailbox.lines_printed = 0u;
        g_next_print_ms = 1000u;
        wt_systick_init();
    }

    for (;;) {
        __asm volatile("wfi");
    }
}
