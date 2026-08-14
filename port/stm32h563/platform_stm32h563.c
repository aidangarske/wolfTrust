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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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

#include "wolftrust/arch/armv8m/spm_svc.h"
#include "wolftrust/ffm.h"
#include "wolftrust/ffm_boot.h"
#include "wolftrust/ffm_domain.h"
#include "psa_manifest/pid.h"

void* memcpy(void* destination, const void* source, size_t size);
void* memset(void* destination, int value, size_t size);

#ifdef WT_ENGINE_HSM
#include "wolftrust/services/hsm.h"
#include "wolftrust/boot_handoff.h"
#include "wolftrust/services/initial_attestation.h"
#include "wolftrust/services/crypto_service.h"
#include <string.h>
#include "wolftrust/arch/armv8m/cmse.h"
#include "wolftrust/arch/armv8m/cmse_transport.h"
#include "wolftrust/sched/tasklet.h"
#include "wolfhsm/wh_error.h"
#include "wolfhsm/wh_transport_mem.h"

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
#define WT_MPU_RLAR_ATTRIDX_NOCACHE (2u << 1)  /* MAIR[2] = normal non-cacheable */

/* MAIR encodings: normal write-back/RA/WA inner+outer = 0xFF;
 * device-nGnRE = 0x04; normal non-cacheable inner+outer = 0x44. */
#define WT_MPU_MAIR0_NORMAL_AT_0   0x000000FFu
#define WT_MPU_MAIR0_DEVICE_AT_1   0x00000400u
#define WT_MPU_MAIR0_NOCACHE_AT_2  0x00440000u

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
#define WT_GUEST_CONTEXT_PSPLIM_NS_OFFSET  68U
#define WT_EXC_RETURN_MODE_THREAD          0x08u
#define WT_EXC_RETURN_RETURN_TO_NONSECURE  0x00u
#define WT_EXC_RETURN_SECURITY_MASK        0x40u

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
_Static_assert(WT_GUEST_CONTEXT_PSPLIM_NS_OFFSET == offsetof(wt_guest_context_t, psplim_ns),
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
static volatile uint32_t g_hsm_wait_skip_count;
static volatile uint32_t g_tasklet_fault_count;
static void (*g_secure_thread_resume_entry)(void) __attribute__((noreturn));

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

    WT_RCC_AHB1ENR |= WT_RCC_AHB1ENR_GTZC1EN;

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
    uint32_t region;
    uint32_t regionCount = WT_SAU_TYPE & 0xFFu;

    /* Disable the SAU before changing any region pair. Updating RBAR while
     * the previous RLAR remains enabled creates a transient region spanning
     * the new base and old limit. That can reclassify the currently executing
     * Secure image as Non-secure before the matching RLAR write completes. */
    WT_SAU_CTRL = 0u;
    wt_dsb();
    wt_isb();

    /* A preceding Secure stage may leave enabled regions behind. Clear every
     * implemented slot before installing wolfTrust's complete attribution
     * map so no higher-priority stale region can override it. */
    for (region = 0u; region < regionCount; region++) {
        WT_SAU_RNR = region;
        WT_SAU_RLAR = 0u;
    }

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

#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
static void wt_clear_boot_handoff_scratch(void)
{
    volatile uint8_t* scratch =
        (volatile uint8_t*)WT_BOOT_HANDOFF_ADDRESS;
    size_t scratchSize = WT_RAM_S_BASE - WT_BOOT_HANDOFF_ADDRESS;
    size_t i;

    for (i = 0u; i < scratchSize; ++i) {
        scratch[i] = 0u;
    }
    wt_dsb();
}
#endif

/* Secure-side MPU whitelist. PRIVDEFENA is OFF, so any access outside
 * the listed regions traps (MemManage / SecureFault). This catches NULL
 * pointer derefs, wild pointer writes, and stray peripheral accesses
 * from inside wolfHSM / wolfCrypt coroutines. Stack overflow is caught
 * separately via PSPLIM_S → UsageFault.STKOF. */
static void wt_mpu_s_init(void)
{
    WT_MPU_S_CTRL = 0u;
    wt_dsb();

    /* MAIR0[7:0]   = Normal WB/RA/WA   (AttrIndx 0)
     * MAIR0[15:8]  = Device nGnRE      (AttrIndx 1)
     * MAIR0[23:16] = Normal non-cacheable (AttrIndx 2) */
    WT_MPU_S_MAIR0 = WT_MPU_MAIR0_NORMAL_AT_0 |
                     WT_MPU_MAIR0_DEVICE_AT_1 |
                     WT_MPU_MAIR0_NOCACHE_AT_2;
    WT_MPU_S_MAIR1 = 0u;

    /* Region 0: secure flash RX (image, NSC stubs, .text). */
    wt_mpu_s_set_region(0u,
        WT_FLASH_S_BASE, WT_FLASH_S_BASE + WT_FLASH_S_SIZE - 1u,
        WT_MPU_RBAR_AP_RO | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NORMAL);

    /* Region 1: secure flash bank 2 RW-NX. The wolfHSM NVM partition
     * lives at 0x0C1FC000..0x0C1FFFFF and STM32H5 flash programming
     * writes data words directly to the destination flash address with
     * FLASH_CR.PG set (the FLASH controller intercepts the stores).
     * The peripheral's own LOCK / PG gating is the real write barrier.
     * Keep this region non-cacheable so an immediate verify reads the flash
     * controller rather than a cache line populated before programming. */
    wt_mpu_s_set_region(1u,
        0x0C100000u, 0x0C1FFFFFu,
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RW | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NOCACHE);

    /* Region 2: secure RAM RW-NX (.data/.bss/MSP_S + coroutine stacks). */
    wt_mpu_s_set_region(2u,
#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
        WT_BOOT_HANDOFF_ADDRESS,
#else
        WT_RAM_S_BASE,
#endif
        WT_RAM_S_BASE + WT_RAM_S_SIZE - 1u,
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

    /* Region 7: Secure alias of guest flash images.
     * RO-XN — the Secure side only reads guest reset vectors and metadata
     * from here; never executes guest code in Secure state. The 0x08...
     * NS alias is reachable too (region 4), but on at least one emulator
     * the Secure-side read of that NS alias returns zero, so we keep this
     * Secure alias window for reliable access. */
    wt_mpu_s_set_region(7u,
        WT_FLASH_TO_S_ALIAS(WT_GUEST0_FLASH_BASE),
        WT_FLASH_TO_S_ALIAS(WT_GUEST1_FLASH_BASE + WT_GUEST_FLASH_SIZE - 1u),
        WT_MPU_RBAR_XN | WT_MPU_RBAR_AP_RO | WT_MPU_RBAR_SH_INNER,
        WT_MPU_RLAR_ATTRIDX_NORMAL);

    /* Enable: PRIVDEFENA=0 (no implicit background region), HFNMIENA=1
     * so MPU stays active during HardFault/NMI (matches what we want
     * since our MemManage handler relies on the same region table). */
    wt_dsb();
    WT_MPU_S_CTRL = WT_MPU_CTRL_HFNMIENA | WT_MPU_CTRL_ENABLE;
    wt_dsb();
    wt_isb();
}

/* Encode one secure MPU region for a Secure Partition domain. Access is
 * granted at any privilege level so the unprivileged partition thread can
 * reach its own regions; isolation comes from which regions are mapped. */
static void wt_program_secure_partition_region(uint32_t rnr, uintptr_t base,
                                               size_t size, uint32_t attributes)
{
    uint32_t rbar_flags = WT_MPU_RBAR_SH_INNER;
    uint32_t rlar_flags = WT_MPU_RLAR_ATTRIDX_NORMAL;

    if ((attributes & WT_MEM_ATTR_EXEC) == 0u) {
        rbar_flags |= WT_MPU_RBAR_XN;
    }
    if ((attributes & WT_MEM_ATTR_WRITE) != 0u) {
        rbar_flags |= WT_MPU_RBAR_AP_RWRW;
    }
    else {
        rbar_flags |= WT_MPU_RBAR_AP_RORO;
    }
    if ((attributes & WT_MEM_ATTR_DEVICE) != 0u) {
        rbar_flags &= ~WT_MPU_RBAR_SH_INNER;
        rlar_flags = WT_MPU_RLAR_ATTRIDX_DEVICE;
    }
    wt_mpu_s_set_region(rnr, base, base + size - 1u, rbar_flags, rlar_flags);
}

static void wt_program_sp_domain_regions(const wt_mpu_region_t* regions,
                                         size_t count, uint32_t ctrl)
{
    size_t i;

    WT_MPU_S_CTRL = 0u;
    wt_dsb();

    WT_MPU_S_MAIR0 = WT_MPU_MAIR0_NORMAL_AT_0 |
                     WT_MPU_MAIR0_DEVICE_AT_1 |
                     WT_MPU_MAIR0_NOCACHE_AT_2;
    WT_MPU_S_MAIR1 = 0u;

    for (i = 0u; i < WT_MAX_MPU_REGIONS; ++i) {
        if (regions != NULL && i < count && regions[i].size != 0u) {
            wt_program_secure_partition_region((uint32_t)i, regions[i].base,
                                               regions[i].size,
                                               regions[i].attributes);
        }
        else {
            WT_MPU_S_RNR  = (uint32_t)i;
            WT_MPU_S_RBAR = 0u;
            WT_MPU_S_RLAR = 0u;
        }
    }

    wt_dsb();
    WT_MPU_S_CTRL = ctrl;
    wt_dsb();
    wt_isb();
}

void wt_platform_program_secure_partition_domain(const wt_mpu_region_t* regions,
                                                 size_t count)
{
    wt_program_sp_domain_regions(regions, count,
                                 WT_MPU_CTRL_HFNMIENA | WT_MPU_CTRL_ENABLE);
}

void wt_platform_program_sp_thread_domain(const wt_mpu_region_t* regions,
                                          size_t count)
{
    /* PRIVDEFENA: the unprivileged SP thread is confined to the mapped
     * regions while the privileged SVC/PendSV/fault handlers keep the
     * default map, so psa_* requests can reach SPM state (WT-FFM-0011). */
    wt_program_sp_domain_regions(regions, count,
                                 WT_MPU_CTRL_PRIVDEFENA |
                                 WT_MPU_CTRL_HFNMIENA | WT_MPU_CTRL_ENABLE);
}

void wt_platform_restore_spm_domain(void)
{
    wt_mpu_s_init();
}

/* Work area placed at the base of the crypto SP secure stack. The execution
 * stack descends from the top of the region toward it; MSPLIM guards the
 * boundary. All fields the isolated compute touches live in this struct so
 * the narrowed domain [secure code RX] + [SP stack RW] fully contains it. */
#define WT_SP_SHA256_DIGEST_SIZE 32u
#define WT_SP_MIN_EXEC_STACK     1024u

typedef struct wt_crypto_sp_work {
    const uint8_t* in;
    uint8_t* out;
    size_t in_len;
    size_t out_len;
    wt_mpu_region_t regions[WT_MAX_MPU_REGIONS];
    size_t region_count;
    int result;
    uint8_t input[WT_CRYPTO_SP_INPUT_MAX];
    uint8_t digest[WT_SP_SHA256_DIGEST_SIZE];
} wt_crypto_sp_work_t;

/* Runs on the crypto SP secure stack after the switch: narrow the secure MPU
 * to the SP domain, compute, restore the SPM domain. Reads only its work
 * struct (SP stack) and secure code/rodata (mapped RX), so it stays inside
 * the narrowed domain the whole time. */
__attribute__((used))
static void wt_crypto_sp_body(wt_crypto_sp_work_t* work)
{
#if defined(WT_FFM_NEGATIVE_PROBE) && (WT_FFM_NEGATIVE_PROBE == 1)
    volatile uint32_t probe;
#endif

    wt_platform_program_secure_partition_domain(work->regions,
                                                work->region_count);
    work->result = wt_crypto_sp_hash(work->in, work->in_len, work->out,
                                     work->out_len);
#if defined(WT_FFM_NEGATIVE_PROBE) && (WT_FFM_NEGATIVE_PROBE == 1)
    /* Negative isolation proof (WT-FFM-0011): a read of SPM-private RAM from
     * inside the narrowed SP domain must fault. Never built into production;
     * gated behind WT_FFM_NEGATIVE_PROBE for the M33MU negative run. */
    probe = *(const volatile uint32_t*)(uintptr_t)WT_RAM_S_BASE;
    (void)probe;
#endif
    wt_platform_restore_spm_domain();
}

/* Switch the active Secure MSP to the SP stack (top r1, limit r2), call the
 * body on it, then restore the caller's MSP and MSPLIM. The MPU narrow/restore
 * happen inside the body so both run on the mapped SP stack. Fails closed to
 * panic if entered on PSP (the SPM dispatch path runs on MSP_S). */
__attribute__((naked, used))
static void wt_crypto_sp_call(wt_crypto_sp_work_t* work, uint32_t sp_top,
                              uint32_t sp_limit)
{
    __asm__ volatile (
        "mrs   r3, control            \n"
        "tst   r3, #2                 \n"
        "bne   1f                     \n"
        "push  {r4, r5, lr}           \n"
        "mov   r4, sp                 \n"
        "mrs   r5, msplim             \n"
        "mov   sp, r1                 \n"
        "msr   msplim, r2             \n"
        "bl    wt_crypto_sp_body      \n"
        "msr   msplim, r5             \n"
        "mov   sp, r4                 \n"
        "pop   {r4, r5, lr}           \n"
        "bx    lr                     \n"
        "1:                           \n"
        "b     wt_platform_panic      \n"
    );
}

int wt_platform_run_crypto_sp_isolated(const uint8_t* input, size_t input_len,
                                       uint8_t* digest, size_t digest_len)
{
    /* Singleton: the SPM dispatch path is single-core with no reentry. */
    static wt_secure_domain_t sp_domain;
    const wt_ffm_runtime_t* ffm;
    const wt_mpu_region_t* stack_region;
    wt_crypto_sp_work_t* work;
    uint32_t sp_top;
    uint32_t sp_limit;
    size_t copy_len;
    size_t region_count;
    size_t i;

    if (input == NULL || digest == NULL ||
            input_len > WT_CRYPTO_SP_INPUT_MAX ||
            digest_len < WT_SP_SHA256_DIGEST_SIZE) {
        return WT_FFM_ERROR_ARGUMENT;
    }

    ffm = wt_ffm_boot_runtime();
    if (ffm == NULL || ffm->manifest == NULL) {
        return WT_FFM_ERROR_STATE;
    }
    if (wt_ffm_resolve_secure_domain(ffm->manifest, PARTITION_CRYPTO_ID,
                                     &sp_domain) != WT_SECURE_DOMAIN_OK) {
        return WT_FFM_ERROR_STATE;
    }

    /* The SP stack and work area come from the manifest domain's writable
     * memory resource; fail closed if the manifest stops declaring one. */
    stack_region = NULL;
    for (i = 0u; i < sp_domain.region_count; i++) {
        if ((sp_domain.regions[i].attributes & WT_MEM_ATTR_WRITE) != 0u &&
                (sp_domain.regions[i].attributes & WT_MEM_ATTR_DEVICE) == 0u) {
            stack_region = &sp_domain.regions[i];
        }
    }
    if (stack_region == NULL ||
            stack_region->size < sizeof(*work) + WT_SP_MIN_EXEC_STACK) {
        return WT_FFM_ERROR_STATE;
    }

    work = (wt_crypto_sp_work_t*)(uintptr_t)stack_region->base;
    sp_top = (uint32_t)stack_region->base + (uint32_t)stack_region->size;

    (void)memset(work, 0, sizeof(*work));
    (void)memcpy(work->input, input, input_len);
    work->in = work->input;
    work->in_len = input_len;
    work->out = work->digest;
    work->out_len = sizeof(work->digest);
    work->result = WT_FFM_ERROR_STATE;

    /* Shared secure code RX (whole image, matches SPM region 0) so the
     * compute's .text and the SHA-256 rodata tables are reachable. The
     * manifest's code window lies inside it and Armv8-M MPU regions must not
     * overlap, so EXEC resources are skipped (task #26 tracks narrowing). */
    work->regions[0].base = WT_FLASH_S_BASE;
    work->regions[0].size = WT_FLASH_S_SIZE;
    work->regions[0].attributes = WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC;
    region_count = 1u;
    for (i = 0u; i < sp_domain.region_count &&
            region_count < WT_MAX_MPU_REGIONS; i++) {
        if ((sp_domain.regions[i].attributes & WT_MEM_ATTR_EXEC) == 0u) {
            work->regions[region_count] = sp_domain.regions[i];
            region_count++;
        }
    }
    work->region_count = region_count;

    sp_limit = ((uint32_t)(uintptr_t)(work + 1) + 7u) & ~7u;
    wt_crypto_sp_call(work, sp_top, sp_limit);

    if (work->result == WT_FFM_SUCCESS) {
        copy_len = digest_len < sizeof(work->digest) ? digest_len :
                   sizeof(work->digest);
        (void)memcpy(digest, work->digest, copy_len);
    }
    return work->result;
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

__attribute__((noreturn, used))
void wt_secure_thread_resume_trampoline(void)
{
    void (*entry)(void) __attribute__((noreturn)) = g_secure_thread_resume_entry;

    if (entry == NULL) {
        wt_platform_panic();
    }

    entry();
    wt_platform_panic();
    __builtin_unreachable();
}

bool wt_platform_in_handler_mode(void)
{
    return wt_read_ipsr() != 0u;
}

bool wt_platform_ns_thread_mode_trap(void)
{
    return (g_live_exc_return &
            (WT_EXC_RETURN_MODE_THREAD | WT_EXC_RETURN_SECURITY_MASK)) ==
           (WT_EXC_RETURN_MODE_THREAD | WT_EXC_RETURN_RETURN_TO_NONSECURE);
}

void wt_platform_return_to_secure_thread(
    void (*entry)(void) __attribute__((noreturn)))
{
    if (entry == NULL || wt_read_ipsr() == 0u) {
        wt_platform_panic();
    }

    g_secure_thread_resume_entry = entry;

    __asm volatile(
        "sub    sp, sp, #32                  \n"
        "movs   r1, #0                       \n"
        "str    r1, [sp, #0]                 \n"
        "str    r1, [sp, #4]                 \n"
        "str    r1, [sp, #8]                 \n"
        "str    r1, [sp, #12]                \n"
        "str    r1, [sp, #16]                \n"
        "str    r1, [sp, #20]                \n"
        "movw   r1, #:lower16:wt_secure_thread_resume_trampoline \n"
        "movt   r1, #:upper16:wt_secure_thread_resume_trampoline \n"
        "str    r1, [sp, #24]                \n"
        "movw   r1, #0x0000                  \n"
        "movt   r1, #0x0100                  \n"
        "str    r1, [sp, #28]                \n"
        "mvn    lr, #6                       \n"
        "bx     lr                           \n"
        :
        : "r"(entry)
        : "memory", "r1");

    __builtin_unreachable();
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
        "ldr r1, [r0, #" WT_ASM_STR(WT_GUEST_CONTEXT_PSPLIM_NS_OFFSET) "] \n"
        "msr psplim_ns, r1              \n"
        /* MSP_NS is the exception stack shared with the secure transition;
         * keep its limit disabled until the port has a dedicated exception
         * stack. Restoring an RTOS task limit here can block the next secure
         * timer frame before the scheduler can switch guests. */
        "mov r1, #0                     \n"
        "msr msplim_ns, r1              \n"
        "ldr r1, [r0, #" WT_ASM_STR(WT_GUEST_CONTEXT_CONTROL_NS_OFFSET) "] \n"
        "msr control_ns, r1             \n"
        "ldr lr, [r0, #" WT_ASM_STR(WT_GUEST_CONTEXT_EXC_RETURN_OFFSET) "] \n"
        "bx lr                          \n"
    );
}

__attribute__((naked, noreturn))
void wt_platform_svc_guest_return(void)
{
    __asm volatile(
        "mrs r2, msp                    \n"
        "ldr r1, =g_secure_entry_sp    \n"
        "str r2, [r1]                  \n"
        "b wt_exception_return_ns_msp  \n"
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
        /* See wt_exception_return_ns_msp — clear PSPLIM_NS / MSPLIM_NS
         * before the first BXNS so a guest that programs them later
         * doesn't inherit a stale value from the previous guest. */
        "msr psplim_ns, r2  \n"
        "msr msplim_ns, r2  \n"
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
#if defined(WT_CONFORMANCE) && (WT_CONFORMANCE == 1)
    wt_spm_sched_hang_probe();
#endif
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
    /* The signed wolfBoot handoff reserves the manifest header at the slot
     * base; the Secure vector table begins at the image base after it. */
    WT_SCB_VTOR_S = WT_FLASH_IMAGE_BASE;
    wt_gtzc_init();
    wt_sau_init();
    wt_mpu_s_init();
    /* Route MemManage and UsageFault to their own handlers (otherwise
     * they escalate to HardFault and we lose the fault-status registers
     * by the time we get the trap). STKOF on PSPLIM_S overflow surfaces
     * as a UsageFault. */
    WT_SCB_SHCSR_S |= WT_SCB_SHCSR_MEMFAULTENA | WT_SCB_SHCSR_USGFAULTENA;
#ifdef WT_ENGINE_HSM
    WT_SCB_SHPR3_S |= (0xFFu << WT_SCB_SHPR3_PENDSV_SHIFT);
    WT_SCB_ICSR_S = WT_SCB_ICSR_PENDSVCLR;
#endif
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
    g_secure_service_depth = 0u;
    g_hsm_wait_skip_count = 0u;
    g_tasklet_fault_count = 0u;
}

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

    /* A non-secure RTOS must not be able to redirect Secure exception
     * dispatch through the shared PPB alias. Reassert wolfTrust's vector
     * table before every guest handoff so the next CMSE/HSM transition
     * always enters the relocated secure runtime. */
    WT_SCB_VTOR_S = WT_FLASH_IMAGE_BASE;
    wt_dsb();
    wt_isb();
    wt_virtual_systick_save_departing();

    WT_SCB_VTOR_NS = (uint32_t)context->vector_table_ns;
    g_active_guest = guest_id;
    wt_virtual_systick_restore_arriving(guest_id);
}

void wt_platform_capture_guest_context(wt_guest_context_t* context,
                                       const wt_trap_frame_t* frame)
{
    wt_exception_frame_t* stacked;
    uintptr_t stacked_addr;

    if (context == NULL || frame == NULL) {
        wt_platform_panic();
    }

    stacked = (wt_exception_frame_t*)frame;
    context->psp_ns = wt_read_psp_ns();
    stacked_addr = (uintptr_t)stacked;
    /* A SecureFault can be raised before the NS exception frame exists (for
     * example, on a failed first BXNS). Preserve the configured guest MSP
     * in that case; replacing it with zero would make the recovery path
     * fabricate a frame at 0xffffffe0 and fault recursively. */
    if (stacked_addr >= WT_RAM_NS_BASE &&
        stacked_addr <= (WT_RAM_NS_BASE + 0x00020000u -
                         sizeof(wt_exception_frame_t))) {
        context->msp_ns = stacked_addr;
    }
    context->control_ns = wt_read_control_ns();
    __asm volatile("mrs %0, psplim_ns" : "=r"(context->psplim_ns));
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
            /* Vector-table reset handlers carry the Thumb marker in bit 0,
             * but an exception frame carries the aligned PC and restores
             * Thumb state from xPSR.T. Leaving bit 0 set causes INVEP on
             * STM32H563 during the first exception-based guest dispatch. */
            stacked->pc = context->pc & ~(uintptr_t)1u;
            stacked->xpsr = context->xpsr;
            context->msp_ns = (uintptr_t)stacked;
            context->frame_stacked = true;
        }
    }

    g_return_context = *context;
    wt_arm_secure_timer();
    if (wt_read_ipsr() == 0u) {
        __asm volatile("svc #0x7F");
        wt_platform_panic();
    }
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

uint32_t wt_platform_active_guest_id(void)
{
    return g_active_guest;
}

void wt_platform_configure_ns_irq(uint32_t irq)
{
    volatile uint32_t *itns = (volatile uint32_t *)0xE000E380u;
    uint32_t word = irq >> 5;
    uint32_t bit  = irq & 31u;

    if (word >= WT_MAX_IRQ_WORDS) return;
    /* ITNS only has a secure alias; mark this IRQ as NS-targeted.
     * Do NOT enable in NVIC ISER here — that comes from the per-guest
     * partition irq_mask when the monitor dispatches a guest that
     * actually wants to receive this IRQ. */
    itns[word] |= (1u << bit);
}

void wt_platform_set_ns_irq_pending(uint32_t irq, bool asserted)
{
    uint32_t word = irq >> 5;
    uint32_t bit  = irq & 31u;
    if (word >= WT_MAX_IRQ_WORDS) return;
    if (asserted) {
        volatile uint32_t *ispr_ns = (volatile uint32_t *)0xE002E200u;
        ispr_ns[word] = (1u << bit);
    } else {
        volatile uint32_t *icpr_ns = (volatile uint32_t *)0xE002E280u;
        icpr_ns[word] = (1u << bit);
    }
}

void Reset_Handler(void)
{
    extern uint32_t _sidata;
    extern uint32_t _sdata;
    extern uint32_t _edata;
    extern uint32_t _sbss;
    extern uint32_t _ebss;
    extern uint32_t _siconfdata;
    extern uint32_t _sconfdata;
    extern uint32_t _econfdata;
    extern uint32_t _sconfbss;
    extern uint32_t _econfbss;
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
    wt_boot_handoff_t bootHandoff;
    int handoffRet;
#endif

    while (dst < &_edata) {
        *dst++ = *src++;
    }

    for (dst = &_sbss; dst < &_ebss; ++dst) {
        *dst = 0u;
    }

    /* Conformance SP .data/.bss live in their own MPU-granted window; the main
     * loops above skip it, so initialize it here. Empty in production builds. */
    src = &_siconfdata;
    for (dst = &_sconfdata; dst < &_econfdata; ++dst) {
        *dst = *src++;
    }
    for (dst = &_sconfbss; dst < &_econfbss; ++dst) {
        *dst = 0u;
    }

    wt_monitor_init();
#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
    handoffRet = wt_boot_handoff_consume(&bootHandoff);
    wt_clear_boot_handoff_scratch();
#endif
#ifdef WT_ENGINE_HSM
    /* Bring up the secure-side wolfHSM service before dispatching guests:
     *  1. tasklet scheduler (provides the bootstrap context)
     *  2. shared wolfCrypt + NVM + lock
     *  3. one transport + server context + tasklet per guest
     * Any failure here is fatal because guests require this engine. */
    wt_tasklet_init();
    if (wt_hsm_init() != 0) wt_platform_panic();
    for (wt_guest_id_t gid = 0u; gid < WT_MAX_GUESTS; gid++) {
        const wt_guest_config_t *configs;
        size_t cfg_count;
        wt_cmse_transport_cfg_t tx_cfg;
        wt_cmse_transport_ctx_t *tx_ctx;
        configs = wt_partitions_config_table(&cfg_count);
        if (configs == NULL || gid >= cfg_count) break;
        if (configs[gid].port.hsm_transport.size == 0u) continue;
        wt_cmse_transport_cfg_for(gid, &tx_cfg);
        tx_ctx = wt_cmse_transport_ctx_for(gid);
        if (tx_ctx == NULL) continue;
        if (wt_hsm_guest_init(gid, &wt_cmse_transport_cb, tx_ctx, &tx_cfg) != 0) {
            wt_platform_panic();
        }
    }
#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
    if (wt_hsm_attest_bootstrap() != WH_ERROR_OK) {
        wt_platform_panic();
    }
#endif
#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
    if (handoffRet == 0) {
        if (wt_initial_attest_init(&bootHandoff) != WT_ATTEST_SUCCESS) {
            wt_platform_panic();
        }
    }
#endif
    /* P1t: crypto SP becomes a scheduled unprivileged coroutine now that
     * the tasklet scheduler exists. Fail closed — guests depend on it. */
    if (wt_ffm_boot_start_sched() != WT_FFM_SUCCESS) {
        wt_platform_panic();
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
 * Secure-side tasklet fault path.
 *
 * MemManage and UsageFault can fire from within a wolfHSM tasklet when:
 *   - PSPLIM_S is hit (UsageFault.STKOF) — tasklet stack overflow,
 *   - MPU_S blocks a wild read/write (MemManage IACCVIOL/DACCVIOL),
 *   - the tasklet executes an illegal instruction (UsageFault).
 *
 * Recovery model:
 *   1. C dispatcher logs the fault, identifies the running tasklet
 *      (g_tasklet_current via wt_tasklet_current), maps it back to a guest_id,
 *      hands the NS client a WH_ERROR_ABORTED via wt_hsm_signal_fault,
 *      drops any mutex held by the dying tasklet, and marks the
 *      tasklet WT_TASKLET_FAULTED.
 *   2. The naked handler asm fabricates a Secure-Thread MSP exception
 *      frame on MSP_S that targets wt_co_fault_recovery_thunk, then
 *      EXC_RETURNs. Hardware lands in the thunk on MSP_S, the thunk
 *      drops the bootstrap's saved r4-r11 frame, returns through the
 *      preserved bootstrap exception frame, and execution resumes
 *      inside wt_tasklet_run as if the tasklet had switched back. The
 *      scheduler picks up the next runnable tasklet — the faulted one
 *      is no longer on the runqueue.
 *
 * If the fault fires while bootstrap (monitor) is running there is no
 * tasklet to abandon and no saved frame to unwind to, so the C
 * dispatcher panics.
 * ----------------------------------------------------------------------- */
static void wt_secure_tasklet_fault_dispatch(void) __attribute__((used));
static void wt_secure_tasklet_fault_dispatch(void)
{
    uint32_t cfsr = WT_SCB_CFSR_S;

    if ((cfsr & WT_SCB_CFSR_MMFSR_MMARVALID) != 0u) {
        g_last_fault_address = WT_SCB_MMFAR_S;
    }
    /* Write-1-to-clear so the next fault is observable. */
    WT_SCB_CFSR_S = cfsr;

    wt_tasklet_t *tasklet = wt_tasklet_current();
    if (tasklet == NULL) {
        /* Bootstrap took the fault — no tasklet to abandon. */
        wt_platform_panic();
    }

    g_tasklet_fault_count++;

    wt_guest_id_t gid = wt_hsm_guest_for_tasklet(tasklet);
    if (gid < WT_MAX_GUESTS) {
        (void)wt_hsm_signal_fault(gid);
    }

    wt_tasklet_mark_faulted(tasklet);
}

/* Shared tail for MemManage_Handler and UsageFault_Handler. Naked so
 * we control the stack layout the EXC_RETURN unwinds through. */
__attribute__((naked, used))
static void wt_secure_tasklet_fault_entry(void)
{
    __asm volatile(
        "bl     wt_secure_tasklet_fault_dispatch    \n"
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
         * tasklet. PSP_S itself is left pointing into the dead
         * tasklet's stack; harmless because CONTROL.SPSEL=0 on
         * return-to-Thread-MSP and arch_switch will overwrite PSP_S
         * before re-enabling PSP. */
        "movs   r0, #0                              \n"
        "msr    psplim, r0                          \n"
        /* An unprivileged Secure Partition coroutine dies here without
         * passing through PendSV's bootstrap path, so clear CONTROL.nPRIV
         * or the resumed bootstrap would run unprivileged. */
        "mrs    r0, control                         \n"
        "bic    r0, r0, #1                          \n"
        "msr    control, r0                         \n"
        /* EXC_RETURN = 0xFFFFFFF9: Secure Thread mode using MSP_S, no
         * FP context. mvn of 6 builds the value with no literal pool. */
        "mvn    lr, #6                              \n"
        "bx     lr                                  \n"
    );
}

__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile("b wt_secure_tasklet_fault_entry \n");
}

__attribute__((naked)) void UsageFault_Handler(void)
{
    __asm volatile("b wt_secure_tasklet_fault_entry \n");
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

void wt_platform_note_hsm_wait_skip(wt_guest_id_t guest_id)
{
    (void)guest_id;
    g_hsm_wait_skip_count++;
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

    /* Mark this guest blocked on HSM work and wake its secure tasklet.
     * The monitor owns tasklet scheduling after this point. */
    wt_monitor_hsm_request_pending(g_active_guest);
    wt_tasklet_t *tasklet = wt_hsm_guest_tasklet(g_active_guest);
    if (tasklet != NULL) {
        wt_tasklet_wake(tasklet);
    }
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
    wt_cmse_transport_ctx_t *tx;

    wt_secure_service_enter();

    rc = wt_hsm_veneer_precheck();
    if (rc != WH_ERROR_OK) goto out;

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

#if defined(WT_ATTEST_COSE) && (WT_ATTEST_COSE == 1)
int WolfTrust_Attest_GetTokenSize_Impl(size_t challengeSize,
                                      size_t* tokenSize);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_Attest_GetTokenSize(size_t challengeSize, size_t* tokenSize)
{
    return WolfTrust_Attest_GetTokenSize_Impl(challengeSize, tokenSize);
}

int WolfTrust_Attest_GetTokenSize_Impl(size_t challengeSize,
                                      size_t* tokenSize)
{
    size_t secureTokenSize = 0u;
    int ret;

    wt_secure_service_enter();
    if ((g_active_guest >= WT_MAX_GUESTS) ||
        !wt_cmse_check_ns_rw(tokenSize, sizeof(*tokenSize)) ||
        !wt_cmse_check_in_guest_ns_ram(g_active_guest, tokenSize,
                                       sizeof(*tokenSize))) {
        ret = WT_ATTEST_ERROR_INVALID_ARGUMENT;
        goto out;
    }

    ret = wt_initial_attest_get_token_size(challengeSize, &secureTokenSize);
    *tokenSize = secureTokenSize;

out:
    wt_secure_service_exit();
    return ret;
}

int WolfTrust_Attest_GetToken_Impl(const uint8_t* challenge,
    size_t challengeSize, uint8_t* token, size_t* tokenSize);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_Attest_GetToken(const uint8_t* challenge, size_t challengeSize,
    uint8_t* token, size_t* tokenSize)
{
    return WolfTrust_Attest_GetToken_Impl(challenge, challengeSize, token,
        tokenSize);
}

int WolfTrust_Attest_GetToken_Impl(const uint8_t* challenge,
    size_t challengeSize, uint8_t* token, size_t* tokenSize)
{
    uint8_t secureChallenge[WT_ATTEST_CHALLENGE_SIZE_64];
    uint8_t secureToken[WT_ATTEST_MAX_TOKEN_SIZE];
    size_t tokenCapacity = 0u;
    size_t secureTokenSize = 0u;
    int ret;

    wt_secure_service_enter();
    if ((g_active_guest >= WT_MAX_GUESTS) ||
        (challengeSize > sizeof(secureChallenge)) ||
        !wt_cmse_check_ns_ro(challenge, challengeSize) ||
        !wt_cmse_check_in_guest_ns_addr(g_active_guest, challenge,
                                        challengeSize) ||
        !wt_cmse_check_ns_rw(tokenSize, sizeof(*tokenSize)) ||
        !wt_cmse_check_in_guest_ns_ram(g_active_guest, tokenSize,
                                       sizeof(*tokenSize))) {
        ret = WT_ATTEST_ERROR_INVALID_ARGUMENT;
        goto out;
    }

    tokenCapacity = *tokenSize;
    if ((tokenCapacity > sizeof(secureToken)) ||
        !wt_cmse_check_ns_rw(token, tokenCapacity) ||
        !wt_cmse_check_in_guest_ns_ram(g_active_guest, token,
                                       tokenCapacity)) {
        ret = WT_ATTEST_ERROR_INVALID_ARGUMENT;
        goto out;
    }

    (void)memcpy(secureChallenge, challenge, challengeSize);
    ret = wt_initial_attest_get_token(g_active_guest, secureChallenge,
        challengeSize, secureToken, tokenCapacity, &secureTokenSize);
    if (ret == WT_ATTEST_SUCCESS) {
        (void)memcpy(token, secureToken, secureTokenSize);
    }
    *tokenSize = secureTokenSize;

out:
    (void)memset(secureChallenge, 0, sizeof(secureChallenge));
    (void)memset(secureToken, 0, sizeof(secureToken));
    wt_secure_service_exit();
    return ret;
}

int WolfTrust_Attest_GetPublicKey_Impl(uint8_t* publicKey,
    size_t publicKeyCapacity, size_t* publicKeySize);
__attribute__((cmse_nonsecure_entry, section(".gnu.sgstubs")))
int WolfTrust_Attest_GetPublicKey(uint8_t* publicKey,
    size_t publicKeyCapacity, size_t* publicKeySize)
{
    return WolfTrust_Attest_GetPublicKey_Impl(publicKey, publicKeyCapacity,
                                               publicKeySize);
}

int WolfTrust_Attest_GetPublicKey_Impl(uint8_t* publicKey,
    size_t publicKeyCapacity, size_t* publicKeySize)
{
    uint8_t securePublicKey[WT_ATTEST_IAK_PUBLIC_KEY_SIZE];
    size_t securePublicKeySize = 0u;
    int ret;

    wt_secure_service_enter();
    if ((g_active_guest >= WT_MAX_GUESTS) ||
        (publicKeyCapacity > sizeof(securePublicKey)) ||
        !wt_cmse_check_ns_rw(publicKey, publicKeyCapacity) ||
        !wt_cmse_check_in_guest_ns_ram(g_active_guest, publicKey,
                                       publicKeyCapacity) ||
        !wt_cmse_check_ns_rw(publicKeySize, sizeof(*publicKeySize)) ||
        !wt_cmse_check_in_guest_ns_ram(g_active_guest, publicKeySize,
                                       sizeof(*publicKeySize))) {
        ret = WT_ATTEST_ERROR_INVALID_ARGUMENT;
        goto out;
    }

    ret = wt_initial_attest_get_iak_public_key(securePublicKey,
        sizeof(securePublicKey), &securePublicKeySize);
    if (ret == WT_ATTEST_SUCCESS) {
        (void)memcpy(publicKey, securePublicKey, securePublicKeySize);
    }
    *publicKeySize = securePublicKeySize;

out:
    (void)memset(securePublicKey, 0, sizeof(securePublicKey));
    wt_secure_service_exit();
    return ret;
}
#endif

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
