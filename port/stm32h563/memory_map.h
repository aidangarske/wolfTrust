/* memory_map.h
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

#ifndef WOLFTRUST_FW_STM32H563_MEMORY_MAP_H
#define WOLFTRUST_FW_STM32H563_MEMORY_MAP_H

#ifndef WT_SECURE_FLASH_BASE
#define WT_SECURE_FLASH_BASE     0x0C000000u
#endif
#ifndef WT_SECURE_FLASH_SIZE
#define WT_SECURE_FLASH_SIZE     0x00020000u
#endif
#ifndef WT_SECURE_IMAGE_HEADER_SIZE
#define WT_SECURE_IMAGE_HEADER_SIZE 0u
#endif
#define WT_FLASH_S_BASE          WT_SECURE_FLASH_BASE
#define WT_FLASH_S_SIZE          WT_SECURE_FLASH_SIZE
#define WT_FLASH_IMAGE_BASE      (WT_FLASH_S_BASE + WT_SECURE_IMAGE_HEADER_SIZE)
#define WT_FLASH_NSC_BASE        (WT_FLASH_IMAGE_BASE + 0x00000400u)
#define WT_FLASH_NSC_END         (WT_FLASH_NSC_BASE + 0x000003FFu)

#define WT_FLASH_NS_BASE         0x08000000u
#define WT_FLASH_S_ALIAS_BASE    0x0C000000u
#define WT_FLASH_TO_S_ALIAS(address) \
    ((address) - WT_FLASH_NS_BASE + WT_FLASH_S_ALIAS_BASE)
#ifndef WT_GUEST0_FLASH_BASE
#define WT_GUEST0_FLASH_BASE     0x08020000u
#endif
#ifndef WT_GUEST1_FLASH_BASE
#define WT_GUEST1_FLASH_BASE     0x08040000u
#endif
#ifndef WT_GUEST_FLASH_SIZE
#define WT_GUEST_FLASH_SIZE      0x00020000u
#endif

/* Secure wolfHSM NVM store.
 *
 * Reserved at the end of internal flash bank 2. STM32H5 sectors are 8 KiB;
 * wolfHSM's flash backend reports one sector as the partition size, and
 * wh_nvm_flash uses two mirrored partitions, so reserve two sectors.
 */
#define WT_FLASH_SECTOR_SIZE       0x00002000u
#define WT_HSM_NVM_FLASH_BASE_NS   0x081FC000u
#define WT_HSM_NVM_FLASH_BASE_S    0x0C1FC000u
#define WT_HSM_NVM_FLASH_SIZE      0x00004000u

/* Conformance-only survive-reset NVM (P5 K2): one reserved sector directly
 * below the wolfHSM NVM store, backing the Arm DRIVER partition's NVMEM so
 * val's boot flag survives an AIRCR.SYSRESETREQ reboot. */
#define WT_CONF_NVM_FLASH_BASE_NS  0x081FA000u
#define WT_CONF_NVM_FLASH_BASE_S   0x0C1FA000u
#define WT_CONF_NVM_FLASH_SIZE     0x00002000u

#define WT_RAM_NS_BASE           0x20000000u
#define WT_GUEST0_RAM_BASE       0x20000000u
#define WT_GUEST1_RAM_BASE       0x20008000u
#define WT_GUEST_RAM_SIZE        0x00008000u

#define WT_RAM_S_BASE            0x30028000u
#define WT_RAM_S_SIZE            0x00080000u

/* Secure per-partition stacks (WT-FFM-0011 Level 3 isolation). Each Secure
 * Partition runs on its own secure stack so the secure MPU can confine it to
 * its own domain. Carved from the top of the secure RAM window that the linker
 * uses (0x30028000 + 440 KiB .. 0x300A0000, the end of physical SRAM); the
 * main stack (_estack) drops to 0x30096000 to make room. Slots 2-4 host the
 * PSA-FF conformance partitions (SERVER/DRIVER/CLIENT) in the conformance
 * build. These MUST match the SPSTACKS region in
 * src/services/wolfhsm/runner/secure.ld. */
#define WT_SP_SECURE_STACK_SIZE  0x00002000u   /* 8 KiB per partition */
#define WT_SP_SECURE_STACK_COUNT 5u
#define WT_SP_SECURE_RAM_SIZE \
    (WT_SP_SECURE_STACK_SIZE * WT_SP_SECURE_STACK_COUNT)
#define WT_SP_SECURE_RAM_BASE    (WT_RAM_S_BASE + 0x0006E000u)  /* 0x30096000 */
#define WT_SP_SECURE_RAM_END \
    (WT_SP_SECURE_RAM_BASE + WT_SP_SECURE_RAM_SIZE)             /* 0x300A0000 */
#define WT_SP_CRYPTO_STACK_BASE \
    (WT_SP_SECURE_RAM_BASE + 0u * WT_SP_SECURE_STACK_SIZE)      /* 0x30096000 */
#define WT_SP_ATTEST_STACK_BASE \
    (WT_SP_SECURE_RAM_BASE + 1u * WT_SP_SECURE_STACK_SIZE)      /* 0x30098000 */
#define WT_SP_FF_SERVER_STACK_BASE \
    (WT_SP_SECURE_RAM_BASE + 2u * WT_SP_SECURE_STACK_SIZE)      /* 0x3009A000 */
#define WT_SP_FF_DRIVER_STACK_BASE \
    (WT_SP_SECURE_RAM_BASE + 3u * WT_SP_SECURE_STACK_SIZE)      /* 0x3009C000 */
#define WT_SP_FF_CLIENT_STACK_BASE \
    (WT_SP_SECURE_RAM_BASE + 4u * WT_SP_SECURE_STACK_SIZE)      /* 0x3009E000 */

/* Conformance Secure-Partition .data/.bss window (P3a). Arm's partition sources
 * keep val_api/psa_api in .data; a hosted SP reaches its own data here while
 * SPM RAM at 0x30028000 stays outside its MPU domain. Sits just below the SP
 * stacks (the linker's RAM window is shortened to 432 KiB to make room); MUST
 * match the CONFDATA region in src/services/wolfhsm/runner/secure.ld. */
#define WT_CONF_SP_DATA_BASE     (WT_RAM_S_BASE + 0x0006B000u)  /* 0x30093000 */
#define WT_CONF_SP_DATA_SIZE     0x00003000u                    /* 12 KiB */

/* Per-partition pseudo-MMIO holes at the top of the CONFDATA window (P4/K4).
 * Each belongs to exactly one Arm conformance partition; the scheduler grants
 * every other SP the window WITHOUT its hole, so the L3 MMIO-isolation panic
 * tests (i047/i055/i057) see a genuine out-of-domain access. MUST match
 * SERVER/DRIVER_PARTITION_MMIO_0_* in conformance/pal_config.h (guarded by an
 * #error cross-check in conformance/conf_nvm_sync.c) and stay above _econfbss
 * (link-time assert in runner/secure.ld). */
#define WT_CONF_SERVER_MMIO_BASE (WT_CONF_SP_DATA_BASE + 0x00002C00u) /* 0x30095C00 */
#define WT_CONF_SERVER_MMIO_SIZE 0x00000100u
#define WT_CONF_DRV_MMIO_BASE    (WT_CONF_SP_DATA_BASE + 0x00002E00u) /* 0x30095E00 */
#define WT_CONF_DRV_MMIO_SIZE    0x00000100u

#define WT_SHARED_STATUS_ADDR    0x20000000u

/* Per-guest CMSE shared transport buffer for wolfHSM. Sits inside each
 * guest's NS RAM window at offset 0x100 (the first 256 bytes are
 * reserved for the existing shared-status mailbox). The secure side
 * validates every access via cmse_check_address_range.
 *
 * Buffer layout (512 bytes total):
 *   [0x000 .. 0x0FF]  request slot  — whCommHeader (8 B) + notify counter + payload
 *   [0x100 .. 0x1FF]  response slot — whCommHeader (8 B) + notify counter + payload
 * Payload size is bounded by WOLFHSM_CFG_COMM_DATA_LEN (256 B).
 * Exact layout in src/arch/armv8m/cmse_transport.c */
#define WT_HSM_BUF_OFFSET        0x00000100u
#define WT_HSM_BUF_SIZE          0x00000200u  /* 512 B = req + resp */
#define WT_GUEST0_HSM_BUF_BASE   (WT_GUEST0_RAM_BASE + WT_HSM_BUF_OFFSET)
#define WT_GUEST1_HSM_BUF_BASE   (WT_GUEST1_RAM_BASE + WT_HSM_BUF_OFFSET)

#endif
