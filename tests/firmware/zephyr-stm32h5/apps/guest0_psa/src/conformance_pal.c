/* conformance_pal.c
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

/* Non-secure PAL + PSA client shim for the Arm psa-arch-tests val NSPE (P3a-4a).
 * The val framework prints through a single char sink (pal_print) and reaches
 * the SPM through the PSA client API, which aliases wolfTrust's CMSE veneers.
 * pal_print_ns is unused (VERBOSITY path funnels to val_printf -> pal_print);
 * the crypto/storage/attestation PALs are unused by the IPC suite and refuse. */

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "psa/client.h"
#include "wolftrust/ffm_veneer.h"

extern int32_t WolfTrust_FFM_Connect(uint32_t sid, uint32_t version);
extern int32_t WolfTrust_FFM_Call(int32_t handle, int32_t type,
                                  wt_ffm_veneer_iovec_t* ns_iovec);
extern void WolfTrust_FFM_Close(int32_t handle);

#if defined(WT_CONF_TRACE)
#define WT_CONF_TRACE_PRINT(...) printk(__VA_ARGS__)
#else
#define WT_CONF_TRACE_PRINT(...)
#endif

psa_handle_t psa_connect(uint32_t sid, uint32_t version)
{
    psa_handle_t handle = (psa_handle_t)WolfTrust_FFM_Connect(sid, version);

    WT_CONF_TRACE_PRINT("wtconf: connect sid=0x%x v=%u -> %d\n", sid, version,
                        (int)handle);
    return handle;
}

void psa_close(psa_handle_t handle)
{
    WT_CONF_TRACE_PRINT("wtconf: close h=%d\n", (int)handle);
    WolfTrust_FFM_Close((int32_t)handle);
}

psa_status_t psa_call(psa_handle_t handle, int32_t type,
                      const psa_invec* in_vec, size_t in_len,
                      psa_outvec* out_vec, size_t out_len)
{
    wt_ffm_veneer_iovec_t iovec;
    psa_status_t status;
    size_t i;

    if (in_len > WT_FFM_VENEER_IOVEC_MAX ||
            out_len > WT_FFM_VENEER_IOVEC_MAX) {
        return PSA_ERROR_PROGRAMMER_ERROR;
    }
    memset(&iovec, 0, sizeof(iovec));
    for (i = 0u; i < in_len; i++) {
        iovec.in[i].base = in_vec[i].base;
        iovec.in[i].len = (uint32_t)in_vec[i].len;
    }
    for (i = 0u; i < out_len; i++) {
        iovec.out[i].base = out_vec[i].base;
        iovec.out[i].len = (uint32_t)out_vec[i].len;
    }
    iovec.in_count = (uint32_t)in_len;
    iovec.out_count = (uint32_t)out_len;
    status = (psa_status_t)WolfTrust_FFM_Call((int32_t)handle, type, &iovec);
    for (i = 0u; i < out_len; i++) {
        out_vec[i].len = iovec.out[i].len;
    }
    WT_CONF_TRACE_PRINT("wtconf: call h=%d in=%u out=%u -> %d\n", (int)handle,
                        (unsigned)in_len, (unsigned)out_len, (int)status);
    return status;
}

/* The val shared status region (PLATFORM_SHARED_REGION_BASE aliases this). The
 * NS framework records per-test status here; a plain BSS buffer suffices. */
uint8_t test_status_buffer[256] = {0};

/* val's log sink: one character at a time to the guest console. */
int pal_print(uint8_t c)
{
    printk("%c", (char)c);
    return 0;
}

/* val records its per-test boot/status state in NVMEM. i001 does not reboot, so
 * a RAM-backed store (erased-flash 0xFF at power-on) satisfies the whole run.
 * Real reset-surviving flash-backed NVM is the reboot-continuity work (P5).
 * val addresses NVMEM with idx*4 offsets; the top index (0xD) needs 56 bytes,
 * so 256 bytes is ample and leaves the guest's 32 KiB window for val's stack. */
#define WT_CONF_NVM_SIZE 0x100u
static uint8_t g_conf_nvm[WT_CONF_NVM_SIZE];
static uint8_t g_conf_nvm_ready;

static void wt_conf_nvm_init(void)
{
    if (g_conf_nvm_ready == 0u) {
        memset(g_conf_nvm, 0xFF, sizeof(g_conf_nvm));
        g_conf_nvm_ready = 1u;
    }
}

int pal_nvm_read(uint32_t offset, void* buffer, size_t size)
{
    wt_conf_nvm_init();
    if (buffer == NULL || (size_t)offset + size > sizeof(g_conf_nvm)) {
        return 1;
    }
    memcpy(buffer, &g_conf_nvm[offset], size);
    return 0;
}

int pal_nvm_write(uint32_t offset, void* buffer, size_t size)
{
    wt_conf_nvm_init();
    if (buffer == NULL || (size_t)offset + size > sizeof(g_conf_nvm)) {
        return 1;
    }
    memcpy(&g_conf_nvm[offset], buffer, size);
    return 0;
}

int pal_watchdog_enable(void)
{
    return 0;
}

int pal_watchdog_disable(void)
{
    return 0;
}

int pal_uart_init_ns(void)
{
    return 0;
}

int pal_wd_timer_init_ns(uint32_t time_us, uint32_t timer_tick_us)
{
    (void)time_us;
    (void)timer_tick_us;
    return 0;
}

int pal_wd_timer_enable_ns(void)
{
    return 0;
}

int pal_wd_timer_disable_ns(void)
{
    return 0;
}

int pal_system_reset(void)
{
    return 0;
}

void pal_terminate_simulation(void)
{
    printk("wolfTrust FF-M conformance: val_entry returned\n");
}

int32_t pal_crypto_function(int type, va_list valist)
{
    (void)type;
    (void)valist;
    return -1;
}

uint32_t pal_its_function(int type, va_list valist)
{
    (void)type;
    (void)valist;
    return (uint32_t)-1;
}

uint32_t pal_ps_function(int type, va_list valist)
{
    (void)type;
    (void)valist;
    return (uint32_t)-1;
}

int32_t pal_attestation_function(int type, va_list valist)
{
    (void)type;
    (void)valist;
    return -1;
}
