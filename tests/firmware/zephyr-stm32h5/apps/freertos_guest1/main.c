/* freertos_guest1 — wolfTrust NS guest running FreeRTOS, exercising the
 * wolfHSM secure-side server through the wolfPKCS11 PKCS#11 surface.
 *
 *   FreeRTOS task → C_*() → wolfPKCS11 → wolfCrypt(WH_DEV_ID) → crypto_cb
 *                → wh_Client_CryptoCb → CMSE Submit/Poll → wolfHSM server
 *
 * Mirrors the Zephyr guest's flow but using PKCS#11 as the front-end
 * (vs PSA in Zephyr) because mainline FreeRTOS expects PKCS#11 via
 * corePKCS11, and wolfPKCS11 already threads a runtime devId through
 * every wc_*Init() — no upstream patch needed (compare with wolfPSA).
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
#include "wolfssl/wolfcrypt/cryptocb.h"

#include "wolfhsm/wh_client.h"
#include "wolfhsm/wh_client_cryptocb.h"

#include "wolfpkcs11/pkcs11.h"

/* External glue. */
int  wolfhsm_guest_init(void);
whClientContext *wolfhsm_guest_client(void);

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

static const uint8_t k_hash_input[] =
    "wolfTrust/FreeRTOS/wolfPKCS11/wolfHSM/CMSE chain test";

static void run_pkcs11_smoke(void)
{
    CK_RV               rv;
    CK_SLOT_ID          slot_id = 0;
    CK_ULONG            slot_count = 0;
    CK_SESSION_HANDLE   session = CK_INVALID_HANDLE;
    CK_MECHANISM        mech = { CKM_SHA256, NULL, 0 };
    CK_BYTE             digest[32];
    CK_ULONG            digest_len = sizeof(digest);

    rv = C_Initialize(NULL);
    uart_puts("freertos_guest1: C_Initialize rv=");
    uart_put_u32((uint32_t)rv);
    uart_puts("\r\n");
    if (rv != CKR_OK) {
        return;
    }

    rv = C_GetSlotList(CK_TRUE, NULL, &slot_count);
    if (rv == CKR_OK && slot_count > 0) {
        CK_SLOT_ID slots[4];
        CK_ULONG   take = slot_count > 4 ? 4 : slot_count;
        rv = C_GetSlotList(CK_TRUE, slots, &take);
        if (rv == CKR_OK) {
            slot_id = slots[0];
        }
    }
    uart_puts("freertos_guest1: C_GetSlotList rv=");
    uart_put_u32((uint32_t)rv);
    uart_puts(" slot_count=");
    uart_put_u32((uint32_t)slot_count);
    uart_puts("\r\n");
    if (rv != CKR_OK) {
        (void)C_Finalize(NULL);
        return;
    }

    rv = C_OpenSession(slot_id, CKF_SERIAL_SESSION, NULL, NULL, &session);
    uart_puts("freertos_guest1: C_OpenSession rv=");
    uart_put_u32((uint32_t)rv);
    uart_puts("\r\n");
    if (rv != CKR_OK) {
        (void)C_Finalize(NULL);
        return;
    }

    rv = C_DigestInit(session, &mech);
    if (rv == CKR_OK) {
        rv = C_DigestUpdate(session, (CK_BYTE_PTR)k_hash_input,
                             sizeof(k_hash_input) - 1);
    }
    if (rv == CKR_OK) {
        rv = C_DigestFinal(session, digest, &digest_len);
    }
    uart_puts("freertos_guest1: C_Digest(SHA-256) rv=");
    uart_put_u32((uint32_t)rv);
    uart_puts(" len=");
    uart_put_u32((uint32_t)digest_len);
    uart_puts(" first=0x");
    if (rv == CKR_OK && digest_len > 0) {
        uart_put_hex_byte(digest[0]);
    } else {
        uart_puts("??");
    }
    uart_puts("\r\n");

    (void)C_CloseSession(session);
    (void)C_Finalize(NULL);
}

static void crypto_task(void *arg)
{
    int rc;
    uint32_t count = 0u;

    (void)arg;

    rc = wolfhsm_guest_init();
    uart_puts("freertos_guest1: wolfhsm_guest_init rc=");
    uart_put_u32((uint32_t)rc);
    uart_puts("\r\n");
    if (rc == 0) {
        (void)wc_CryptoCb_RegisterDevice(WH_DEV_ID, wh_Client_CryptoCb,
                                          wolfhsm_guest_client());
        uart_puts("freertos_guest1: wolfHSM client up; devId=0x5748534d\r\n");
    }

    run_pkcs11_smoke();

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

    /* 2048 stack words = 8 KiB. wolfPKCS11's C_Initialize + the
     * first wolfCrypt RNG init through the secure side burns ~5 KiB
     * in peak call depth; 4 KiB triggers a STKOF on the FreeRTOS
     * port's PSPLIM guard. */
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

/* wolfPKCS11 exposes WP11_PBKDF2 / WP11_PKCS12_PBKDF wrappers that call
 * into wolfCrypt's pwdbased / pkcs12 implementations. Our wolfCrypt
 * subset omits both (NO_PWDBASED / NO_PKCS12 in user_settings.h), and
 * the wrappers are never reached at runtime because the demo never
 * touches token storage / PIN derivation paths. Provide local stubs so
 * the link resolves; --gc-sections is expected to drop them as dead
 * code in practice. */
typedef unsigned char byte;
int wc_PBKDF2(byte *output, const byte *passwd, int pLen,
              const byte *salt, int sLen, int iterations, int kLen,
              int hashType);
int wc_PBKDF2(byte *output, const byte *passwd, int pLen,
              const byte *salt, int sLen, int iterations, int kLen,
              int hashType)
{
    (void)output; (void)passwd; (void)pLen; (void)salt; (void)sLen;
    (void)iterations; (void)kLen; (void)hashType;
    return -1;
}

int wc_PKCS12_PBKDF(byte *output, const byte *passwd, int pLen,
                    const byte *salt, int sLen, int iterations, int kLen,
                    int hashType, int purpose);
int wc_PKCS12_PBKDF(byte *output, const byte *passwd, int pLen,
                    const byte *salt, int sLen, int iterations, int kLen,
                    int hashType, int purpose)
{
    (void)output; (void)passwd; (void)pLen; (void)salt; (void)sLen;
    (void)iterations; (void)kLen; (void)hashType; (void)purpose;
    return -1;
}
