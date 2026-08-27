/* freertos_guest1 — wolfTrust NS guest running FreeRTOS, exercising the
 * FF-M SPM through the OS-neutral PSA client core (P7-S3).
 *
 *   FreeRTOS task → psa_* (wolfPSA) / psa_connect+psa_call (neutral core)
 *                → WolfTrust_FFM_* veneers → SPM → SERVICE_CRYPTO → vault
 *
 * The raw HSM-CMSE transport (wolfPKCS11 → wh_Client_CryptoCb →
 * WolfTrust_HSM_Submit/Poll) is retired from this guest: every secure
 * request is mediated by the SPM (WT-FFM-0054), and the wolfCrypt DRBG
 * seeds from SERVICE_CRYPTO's vault-backed RNG over the same path.
 *
 * Cortex-M33 NTZ port: TrustZone-unaware FreeRTOS port. The secure side
 * still owns CMSE, the secure SysTick, and the per-guest MPU window.
 * FreeRTOS just sees a flat NS world driven by its own SysTick.
 *
 * Heartbeats use a busy-delay sized for the m33mu emulator pace,
 * matching the baremetal guest1.c approach — NS-SysTick paravirt under
 * the HSM scheduler is unreliable for guest-driven timing, so the
 * heartbeat doesn't trust vTaskDelay(). Crypto runs once at boot. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "wolfssl/wolfcrypt/settings.h"
#include "wolfssl/wolfcrypt/random.h"

#include <psa/crypto.h>

#include "psa/client.h"
#include "wolftrust/ffm_crypto_client.h"

#define WT_CRYPTO_SID 4097u

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

/* USART3 — same shared UART the secure side and guest0 use. */
#define USART3_BASE          0x40004800u
#define USART_CR1(base)      (*(volatile uint32_t *)((base) + 0x00u))
#define USART_BRR(base)      (*(volatile uint32_t *)((base) + 0x0Cu))
#define USART_ISR(base)      (*(volatile uint32_t *)((base) + 0x1Cu))
#define USART_TDR(base)      (*(volatile uint32_t *)((base) + 0x28u))
#define USART_CR1_UE         (1u << 0)
#define USART_CR1_RE         (1u << 2)
#define USART_CR1_TE         (1u << 3)
#define USART_ISR_TXE        (1u << 7)

#ifndef WT_FREERTOS_HEARTBEAT_SPIN
#define WT_FREERTOS_HEARTBEAT_SPIN 27000000u
#endif

/* ---- vector table + reset path ---------------------------------------- */

static void default_handler(void)
{
    for (;;) {}
}

void Reset_Handler(void);
void NMI_Handler(void) __attribute__((weak, alias("default_handler")));
void HardFault_Handler(void) __attribute__((weak, alias("default_handler")));
void MemManage_Handler(void) __attribute__((weak, alias("default_handler")));
void BusFault_Handler(void) __attribute__((weak, alias("default_handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("default_handler")));

/* FreeRTOS provides these as `vPortSVCHandler` / `xPortPendSVHandler`
 * / `xPortSysTickHandler`; FreeRTOSConfig.h aliases them onto the
 * standard CMSIS exception-vector names, so we wire them directly here. */
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

void DebugMon_Handler(void) __attribute__((weak, alias("default_handler")));

__attribute__((section(".vectors")))
const uint32_t g_vectors[16] = {
    [0]  = (uint32_t)&_estack,
    [1]  = (uint32_t)&Reset_Handler,
    [2]  = (uint32_t)&NMI_Handler,
    [3]  = (uint32_t)&HardFault_Handler,
    [4]  = (uint32_t)&MemManage_Handler,
    [5]  = (uint32_t)&BusFault_Handler,
    [6]  = (uint32_t)&UsageFault_Handler,
    [11] = (uint32_t)&SVC_Handler,
    [12] = (uint32_t)&DebugMon_Handler,
    [14] = (uint32_t)&PendSV_Handler,
    [15] = (uint32_t)&SysTick_Handler,
};

static void copy_data(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
}

static void zero_bss(void)
{
    uint32_t *dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }
}

/* ---- UART ------------------------------------------------------------- */

static void uart_init(void)
{
    USART_CR1(USART3_BASE) = 0u;
    /* Matches baremetal guest1's BRR pin — m33mu accepts anything nonzero. */
    USART_BRR(USART3_BASE) = 0x410u;
    USART_CR1(USART3_BASE) = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart_putc(char c)
{
    while ((USART_ISR(USART3_BASE) & USART_ISR_TXE) == 0u) {}
    USART_TDR(USART3_BASE) = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

static void uart_put_u32(uint32_t value)
{
    char buf[10];
    uint32_t i = 0u;

    if (value == 0u) {
        uart_putc('0');
        return;
    }
    while (value != 0u) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (i > 0u) {
        uart_putc(buf[--i]);
    }
}

static void uart_put_hex_byte(uint8_t b)
{
    static const char hex[] = "0123456789abcdef";
    uart_putc(hex[(b >> 4) & 0xfu]);
    uart_putc(hex[b & 0xfu]);
}

static void busy_delay(uint32_t iters)
{
    volatile uint32_t i;
    for (i = 0u; i < iters; i++) {
        __asm volatile ("nop");
    }
}

/* ---- the crypto task -------------------------------------------------- */

static void uart_put_i32(int32_t value)
{
    if (value < 0) {
        uart_putc('-');
        uart_put_u32((uint32_t)(-value));
    } else {
        uart_put_u32((uint32_t)value);
    }
}

static int buf_is_zero(const uint8_t *buf, size_t len)
{
    size_t i;

    for (i = 0u; i < len; i++) {
        if (buf[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

/* The same KAT guest0's exercise_ffm_crypto proves: SHA-256 through
 * SERVICE_CRYPTO's mediated dispatch, now from the FreeRTOS client. */
static const uint8_t k_hash_input[] =
    "wolfTrust FF-M SERVICE_CRYPTO dispatch test";
static const uint8_t k_hash_expected[32] = {
    0x20, 0x03, 0xdf, 0x15, 0x2a, 0x52, 0x8a, 0x06,
    0xc8, 0xd3, 0x48, 0xb8, 0xfa, 0x8b, 0x2f, 0x87,
    0xf7, 0x1f, 0xae, 0xc6, 0x24, 0x6c, 0x7e, 0x72,
    0x8e, 0x27, 0xa4, 0xb5, 0x0a, 0x49, 0x84, 0x66
};

static void run_ffm_sha256_kat(void)
{
    psa_handle_t handle;
    psa_invec in_vec;
    psa_outvec out_vec;
    psa_status_t st;
    uint8_t digest[32];

    handle = psa_connect(WT_CRYPTO_SID, 1u);
    if (handle <= 0) {
        uart_puts("freertos_guest1: ffm connect FAILED handle=");
        uart_put_i32((int32_t)handle);
        uart_puts("\r\n");
        return;
    }
    in_vec.base = k_hash_input;
    in_vec.len = sizeof(k_hash_input) - 1u;
    out_vec.base = digest;
    out_vec.len = sizeof(digest);
    memset(digest, 0, sizeof(digest));
    st = psa_call(handle, 0, &in_vec, 1u, &out_vec, 1u);
    if (st == PSA_SUCCESS && out_vec.len == sizeof(digest) &&
        memcmp(digest, k_hash_expected, sizeof(digest)) == 0) {
        uart_puts("freertos_guest1: ffm sha256 ok first=0x");
        uart_put_hex_byte(digest[0]);
        uart_puts("\r\n");
    } else {
        uart_puts("freertos_guest1: ffm sha256 FAILED st=");
        uart_put_i32((int32_t)st);
        uart_puts("\r\n");
    }
    psa_close(handle);
}

/* Direct FF-M proof of the vault-backed RNG op, independent of the
 * wolfCrypt DRBG layering above it. */
static void run_ffm_rng(void)
{
    uint8_t buf[32];

    memset(buf, 0, sizeof(buf));
    if (wt_ffm_crypto_random(WT_CRYPTO_SID, buf, sizeof(buf)) == 0 &&
        buf_is_zero(buf, sizeof(buf)) == 0) {
        uart_puts("freertos_guest1: ffm rng ok\r\n");
    } else {
        uart_puts("freertos_guest1: ffm rng FAILED\r\n");
    }
}

/* PSA Crypto API parity with guest0 (wolfPSA front-end): the DRBG behind
 * psa_generate_random seeds through the FF-M RNG hook below, so the
 * entropy crossing is SPM-mediated too. */
static void run_psa_smoke(void)
{
    psa_status_t st;
    uint8_t buf[32];
    uint8_t digest[32];
    size_t digest_len = 0u;

    st = psa_crypto_init();
    uart_puts("freertos_guest1: psa_crypto_init st=");
    uart_put_i32((int32_t)st);
    uart_puts("\r\n");
    if (st != PSA_SUCCESS) {
        return;
    }

    memset(buf, 0, sizeof(buf));
    st = psa_generate_random(buf, sizeof(buf));
    if (st == PSA_SUCCESS && buf_is_zero(buf, sizeof(buf)) == 0) {
        uart_puts("freertos_guest1: psa rng ok\r\n");
    } else {
        uart_puts("freertos_guest1: psa rng FAILED st=");
        uart_put_i32((int32_t)st);
        uart_puts("\r\n");
    }

    memset(digest, 0, sizeof(digest));
    st = psa_hash_compute(PSA_ALG_SHA_256, k_hash_input,
                          sizeof(k_hash_input) - 1u, digest, sizeof(digest),
                          &digest_len);
    if (st == PSA_SUCCESS && digest_len == sizeof(digest) &&
        memcmp(digest, k_hash_expected, sizeof(digest)) == 0) {
        uart_puts("freertos_guest1: psa hash ok\r\n");
    } else {
        uart_puts("freertos_guest1: psa hash FAILED st=");
        uart_put_i32((int32_t)st);
        uart_puts("\r\n");
    }
}

static void crypto_task(void *arg)
{
    uint32_t count = 0u;

    (void)arg;

    run_ffm_sha256_kat();
    run_ffm_rng();
    run_psa_smoke();

    for (;;) {
        busy_delay(WT_FREERTOS_HEARTBEAT_SPIN);
        uart_puts("freertos_guest1: heartbeat ");
        uart_put_u32(count);
        uart_puts("\r\n");
        count++;
    }
}

/* ---- entry ------------------------------------------------------------ */

__attribute__((section(".reset")))
void Reset_Handler(void)
{
    copy_data();
    zero_bss();
    uart_init();
    uart_puts("freertos_guest1: alive\r\n");

    /* 2048 stack words = 8 KiB. The first wolfCrypt DRBG init through
     * the FF-M RNG hook plus wolfPSA hash setup burns several KiB in
     * peak call depth; 4 KiB triggered a STKOF on the FreeRTOS port's
     * PSPLIM guard under the old transport, so keep the headroom. */
    if (xTaskCreate(crypto_task, "crypto", 2048, NULL,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        uart_puts("freertos_guest1: xTaskCreate failed\r\n");
        for (;;) {}
    }

    vTaskStartScheduler();

    /* If we get here, the scheduler returned (out of heap / idle task
     * creation failed). Park forever — the secure side preempts us
     * anyway when the next timeslice fires. */
    uart_puts("freertos_guest1: scheduler exited\r\n");
    for (;;) {}
}

/* FreeRTOS hooks that the build needs symbols for even with the hook
 * Kconfig knobs disabled. Mostly stubs. */

void vAssertCalled(const char *file, int line)
{
    (void)file; (void)line;
    uart_puts("freertos_guest1: assertion failed\r\n");
    for (;;) {}
}

void vApplicationMallocFailedHook(void)
{
    uart_puts("freertos_guest1: malloc failed\r\n");
    for (;;) {}
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task; (void)name;
    uart_puts("freertos_guest1: stack overflow\r\n");
    for (;;) {}
}

/* FF-M entropy hooks (WT-FFM-0054): user_settings.h maps
 * CUSTOM_RAND_GENERATE_BLOCK here, and wolfCrypt's random.c references
 * wc_GenerateSeed for DRBG reseed. Both draw from SERVICE_CRYPTO's
 * vault-backed RNG through the SPM — this guest has no raw transport
 * to the secure side at all. */
int wolftrust_guest_ffm_rng(unsigned char *output, unsigned int sz)
{
    if (output == NULL && sz != 0u) {
        return -1;
    }
    if (sz == 0u) {
        return 0;
    }
    return wt_ffm_crypto_random(WT_CRYPTO_SID, output, (size_t)sz);
}

int wc_GenerateSeed(OS_Seed *os, byte *output, word32 sz)
{
    (void)os;
    return wolftrust_guest_ffm_rng((unsigned char *)output,
                                   (unsigned int)sz);
}
