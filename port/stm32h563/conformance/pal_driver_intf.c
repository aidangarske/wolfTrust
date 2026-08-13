/* pal_driver_intf.c
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* SPE PAL for the Arm psa-arch-tests DRIVER partition on wolfTrust (P3a).
 * NVMEM is a RAM-backed store (reset-surviving flash NVM is the P5 reboot
 * work); the watchdog and interrupt hooks are no-ops until P4/P6 provide the
 * real devices; prints are swallowed until the secure UART routing lands in
 * P3b. All state lives in the driver partition's own CONFDATA/.bss window. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uintptr_t addr_t;

#define WT_CONF_DRV_NVM_SIZE 0x100u
static uint8_t g_drv_nvm[WT_CONF_DRV_NVM_SIZE];
static uint8_t g_drv_nvm_ready;
static uint8_t g_drv_wd_enabled;

static int wt_conf_drv_nvm_init(void)
{
    if (g_drv_nvm_ready == 0u) {
        (void)memset(g_drv_nvm, 0xFF, sizeof(g_drv_nvm));
        g_drv_nvm_ready = 1u;
    }
    return 0;
}

void pal_uart_init(uint32_t uart_base_addr)
{
    (void)uart_base_addr;
}

void pal_print_s(const char* str, int32_t data)
{
    (void)str;
    (void)data;
}

int pal_print(uint8_t c)
{
    (void)c;
    return 0;
}

int pal_nvmem_write(addr_t base, uint32_t offset, void* buffer, int size)
{
    (void)base;
    (void)wt_conf_drv_nvm_init();
    if (buffer == NULL || size < 0 ||
            (size_t)offset + (size_t)size > sizeof(g_drv_nvm)) {
        return 0;
    }
    (void)memcpy(&g_drv_nvm[offset], buffer, (size_t)size);
    return 1;
}

int pal_nvmem_read(addr_t base, uint32_t offset, void* buffer, int size)
{
    (void)base;
    (void)wt_conf_drv_nvm_init();
    if (buffer == NULL || size < 0 ||
            (size_t)offset + (size_t)size > sizeof(g_drv_nvm)) {
        return 0;
    }
    (void)memcpy(buffer, &g_drv_nvm[offset], (size_t)size);
    return 1;
}

int pal_wd_timer_init(addr_t base_addr, uint32_t time_us,
                      uint32_t timer_tick_us)
{
    (void)base_addr;
    (void)time_us;
    (void)timer_tick_us;
    return 0;
}

int pal_wd_timer_enable(addr_t base_addr)
{
    (void)base_addr;
    g_drv_wd_enabled = 1u;
    return 0;
}

int pal_wd_timer_disable(addr_t base_addr)
{
    (void)base_addr;
    g_drv_wd_enabled = 0u;
    return 0;
}

int pal_wd_timer_is_enabled(addr_t base_addr)
{
    (void)base_addr;
    return (int)g_drv_wd_enabled;
}

void pal_generate_interrupt(void)
{
}

void pal_disable_interrupt(void)
{
}
