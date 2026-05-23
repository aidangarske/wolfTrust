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

#include "memory_map.h"

#ifdef WT_ENGINE_HSM
#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/ecc.h"
#include "wolfssl/wolfcrypt/random.h"
#include "wolfssl/wolfcrypt/hash.h"
#include "wolfhsm/wh_client.h"

int wolfhsm_guest_init(void);

/* Static buffers to keep large structs off the stack. */
static ecc_key  s_ecc_key;
static WC_RNG   s_rng;
static uint8_t  s_sig[80];
static uint8_t  s_digest[WC_SHA256_DIGEST_SIZE];
#endif /* WT_ENGINE_HSM */

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
#define USART_CR1_RE          (1u << 2)
#define USART_CR1_TE          (1u << 3)
#define USART_ISR_TXE         (1u << 7)

#ifndef WT_GUEST_CORE_CLOCK_HZ
#define WT_GUEST_CORE_CLOCK_HZ 240000000u
#endif

#ifndef WT_GUEST_UART_CLOCK_HZ
#define WT_GUEST_UART_CLOCK_HZ 120000000u
#endif

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
#if WT_SHARED_UART == 1 || WT_SHARED_UART == 2
    return USART2_BASE;
#elif WT_SHARED_UART == 3
    return USART3_BASE;
#else
    return ((uintptr_t)&_estack == (WT_GUEST1_RAM_BASE + WT_GUEST_RAM_SIZE)) ?
           USART3_BASE : USART2_BASE;
#endif
}

static uint32_t wt_guest_id(void)
{
    return ((uintptr_t)&_estack == (WT_GUEST1_RAM_BASE + WT_GUEST_RAM_SIZE)) ?
           1u : 0u;
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
    uint32_t brr = WT_GUEST_UART_CLOCK_HZ / 115200u;

    USART_CR1(base) = 0u;
    USART_CR2(base) = 0u;
    USART_CR3(base) = 0u;
    USART_BRR(base) = brr;
    USART_CR1(base) = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
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
    wt_uart_put_u32(wt_guest_id());
    wt_uart_putc(':');
    wt_uart_put_u32(second_mark);
    wt_uart_putc('\n');
}

static void wt_systick_init(void)
{
    SYST_CSR = 0u;
    SYST_RVR = (WT_GUEST_CORE_CLOCK_HZ / 1000u) - 1u;
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

#ifdef WT_ENGINE_HSM

static void print_str(const char *s)
{
    while (*s != '\0') {
        wt_uart_putc(*s++);
    }
}

static void print_hex_byte(uint8_t b)
{
    static const char hex[] = "0123456789abcdef";
    wt_uart_putc(hex[(b >> 4) & 0xFu]);
    wt_uart_putc(hex[b & 0xFu]);
}

/* Fixed 32-byte input for SHA-256: bytes 0x00..0x1F */
static const uint8_t s_hash_input[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

static void run_hsm_selftest(void)
{
    int      rc;
    word32   sig_len;
    int      verify_ok = 0;

    /* --- Step 1: Init wolfHSM client --- */
    rc = wolfhsm_guest_init();
    if (rc != 0) {
        wt_uart_putc('g');
        wt_uart_put_u32(wt_guest_id());
        print_str(":hsm-init ");
        wt_uart_put_u32((uint32_t)rc);
        wt_uart_putc('\n');
        return; /* fall through to heartbeat loop */
    }

    /* --- Step 2: Init RNG via HSM --- */
    rc = wc_InitRng_ex(&s_rng, NULL, WH_DEV_ID);
    if (rc != 0) {
        wt_uart_putc('g');
        wt_uart_put_u32(wt_guest_id());
        print_str(":HSM FAIL ");
        wt_uart_put_u32((uint32_t)rc);
        wt_uart_putc('\n');
        return;
    }

    /* --- Step 3: Init ECC key via HSM --- */
    rc = wc_ecc_init_ex(&s_ecc_key, NULL, WH_DEV_ID);
    if (rc != 0) {
        wc_FreeRng(&s_rng);
        wt_uart_putc('g');
        wt_uart_put_u32(wt_guest_id());
        print_str(":HSM FAIL ");
        wt_uart_put_u32((uint32_t)rc);
        wt_uart_putc('\n');
        return;
    }

    /* --- Step 4: Generate P-256 key via HSM --- */
    rc = wc_ecc_make_key_ex(&s_rng, 32, &s_ecc_key, ECC_SECP256R1);
    if (rc != 0) {
        wc_ecc_free(&s_ecc_key);
        wc_FreeRng(&s_rng);
        wt_uart_putc('g');
        wt_uart_put_u32(wt_guest_id());
        print_str(":HSM FAIL ");
        wt_uart_put_u32((uint32_t)rc);
        wt_uart_putc('\n');
        return;
    }

    /* --- Step 5: SHA-256 locally (proves guest wolfCrypt works) --- */
    rc = wc_Sha256Hash(s_hash_input, sizeof(s_hash_input), s_digest);
    if (rc != 0) {
        wc_ecc_free(&s_ecc_key);
        wc_FreeRng(&s_rng);
        wt_uart_putc('g');
        wt_uart_put_u32(wt_guest_id());
        print_str(":HSM FAIL ");
        wt_uart_put_u32((uint32_t)rc);
        wt_uart_putc('\n');
        return;
    }

    /* --- Step 6: Sign the digest via HSM --- */
    sig_len = (word32)sizeof(s_sig);
    rc = wc_ecc_sign_hash(s_digest, WC_SHA256_DIGEST_SIZE,
                          s_sig, &sig_len, &s_rng, &s_ecc_key);
    if (rc != 0) {
        wc_ecc_free(&s_ecc_key);
        wc_FreeRng(&s_rng);
        wt_uart_putc('g');
        wt_uart_put_u32(wt_guest_id());
        print_str(":HSM FAIL ");
        wt_uart_put_u32((uint32_t)rc);
        wt_uart_putc('\n');
        return;
    }

    /* --- Step 7: Verify the signature via HSM --- */
    rc = wc_ecc_verify_hash(s_sig, sig_len,
                            s_digest, WC_SHA256_DIGEST_SIZE,
                            &verify_ok, &s_ecc_key);

    wc_ecc_free(&s_ecc_key);
    wc_FreeRng(&s_rng);

    if (rc != 0 || verify_ok != 1) {
        wt_uart_putc('g');
        wt_uart_put_u32(wt_guest_id());
        print_str(":HSM FAIL ");
        wt_uart_put_u32((uint32_t)rc);
        wt_uart_putc('\n');
        return;
    }

    /* --- Step 8: Success --- */
    wt_uart_putc('g');
    wt_uart_put_u32(wt_guest_id());
    print_str(":sig[0]=");
    print_hex_byte(s_sig[0]);
    wt_uart_putc('\n');

    wt_uart_putc('g');
    wt_uart_put_u32(wt_guest_id());
    print_str(":HSM OK\n");

    g_mailbox.signature = 0x47534D4Fu | (wt_guest_id() << 24);
}

#endif /* WT_ENGINE_HSM */

__attribute__((section(".reset")))
void Reset_Handler(void)
{
    if (g_mailbox.boot_count == 0u) {
        wt_copy_data();
        wt_zero_bss();
        wt_uart_init();

        g_mailbox.boot_count = 1u;
        g_mailbox.signature = 0x47554530u + wt_guest_id();
        g_mailbox.heartbeat = 0u;
        g_mailbox.virtual_ms = 0u;
        g_mailbox.lines_printed = 0u;
        g_next_print_ms = 1000u;

#ifdef WT_ENGINE_HSM
        run_hsm_selftest();
#endif

        wt_systick_init();
    }

    for (;;) {
        __asm volatile("wfi");
    }
}
