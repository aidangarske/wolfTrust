/* guest1.c — baremetal STM32H563 NS heartbeat companion for the Zephyr
 * wolfTrust demo. Coexists with the Zephyr guest0 image as the wolfTrust
 * guest-b partition; emits a counter on the shared USART3 every five
 * (emulated) seconds, with no wolfHSM or CMSE traffic of its own.
 *
 * Timing uses a calibrated busy loop rather than the NS SysTick: in the
 * HSM scheduling mode this demo runs under, the secure side paravirtualises
 * SysTick and most CPU is spent on HSM service work, so NS SysTick ticks
 * aren't a reliable wall-clock source for a baremetal guest. The constant
 * below is tuned for the m33mu STM32H563 model. */

#include <stdint.h>

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

#define USART3_BASE          0x40004800u
#define USART_CR1(base)      (*(volatile uint32_t *)((base) + 0x00u))
#define USART_CR2(base)      (*(volatile uint32_t *)((base) + 0x04u))
#define USART_CR3(base)      (*(volatile uint32_t *)((base) + 0x08u))
#define USART_BRR(base)      (*(volatile uint32_t *)((base) + 0x0Cu))
#define USART_ISR(base)      (*(volatile uint32_t *)((base) + 0x1Cu))
#define USART_TDR(base)      (*(volatile uint32_t *)((base) + 0x28u))

#define USART_CR1_UE         (1u << 0)
#define USART_CR1_RE         (1u << 2)
#define USART_CR1_TE         (1u << 3)
#define USART_ISR_TXE        (1u << 7)

/* Loop iterations that approximate a 5-second cadence under m33mu's
 * STM32H563 emulation when sharing CPU with the Zephyr guest. Override
 * at build time to retune for a different host / emulator. */
#ifndef WT_GUEST1_HEARTBEAT_SPIN
#define WT_GUEST1_HEARTBEAT_SPIN 27000000u
#endif

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
void SysTick_Handler(void) __attribute__((weak, alias("default_handler")));

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
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

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

static void uart_init(void)
{
    USART_CR1(USART3_BASE) = 0u;
    USART_CR2(USART3_BASE) = 0u;
    USART_CR3(USART3_BASE) = 0u;
    /* m33mu accepts any nonzero BRR and ignores the divisor; on hardware
     * this is WT_GUEST_UART_CLOCK_HZ / 115200, but pinning it here keeps
     * the file self-contained. */
    USART_BRR(USART3_BASE) = 0x410u;
    USART_CR1(USART3_BASE) = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void uart_putc(char c)
{
    while ((USART_ISR(USART3_BASE) & USART_ISR_TXE) == 0u) {
    }
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

static void busy_delay(uint32_t iters)
{
    volatile uint32_t i;

    for (i = 0u; i < iters; i++) {
        __asm volatile ("nop");
    }
}

__attribute__((section(".reset")))
void Reset_Handler(void)
{
    uint32_t count = 0u;

    copy_data();
    zero_bss();
    uart_init();

    uart_puts("guest1: alive\r\n");

    for (;;) {
        busy_delay(WT_GUEST1_HEARTBEAT_SPIN);
        uart_puts("guest1: heartbeat ");
        uart_put_u32(count);
        uart_puts("\r\n");
        count++;
    }
}
