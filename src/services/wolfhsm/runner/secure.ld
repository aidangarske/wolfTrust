/* secure.ld
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

ENTRY(Reset_Handler)

MEMORY {
  FLASH (rx) : ORIGIN = (WT_SECURE_FLASH_ORIGIN + WT_SECURE_IMAGE_HEADER_SIZE),
               LENGTH = (WT_SECURE_FLASH_SIZE - WT_SECURE_IMAGE_HEADER_SIZE)
  /* SRAM1 has Secure and Non-secure aliases to the same physical memory.
   * The first 32 KiB are assigned to the two guests through the 0x20000000
   * Non-secure alias, so keep secure .data/.bss above that physical window. */
  RAM   (rwx): ORIGIN = 0x30028000, LENGTH = 420K
  /* Vault partition stack (WT-FFM-0047): the privileged vault coroutine's
   * manifest-declared execution stack (WT_SP_VAULT_STACK_* in memory_map.h). */
  VAULTSTACK (rw): ORIGIN = 0x30091000, LENGTH = 8K
  /* Conformance Secure-Partition .data/.bss (P3a). Arm's partition sources hold
   * their val_api/psa_api tables in .data; a hosted SP must reach its own data
   * but not the SPM's, so it lives in this 8 KiB window (WT_CONF_SP_DATA_* in
   * memory_map.h) that the SP MPU domain grants while SPM RAM at 0x30028000
   * stays denied. Empty in non-conformance builds. */
  CONFDATA (rw): ORIGIN = 0x30093000, LENGTH = 12K
  /* Secure per-partition stacks (WT-FFM-0011). The top 40 KiB of the secure
   * RAM window, reserved so each Secure Partition runs on its own secure
   * stack the MPU can confine. Mirrors WT_SP_SECURE_RAM_* in memory_map.h. */
  SPSTACKS (rw): ORIGIN = 0x30096000, LENGTH = 40K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sidata = LOADADDR(.data);

SECTIONS {
  .vectors (WT_SECURE_FLASH_ORIGIN + WT_SECURE_IMAGE_HEADER_SIZE) : {
    KEEP(*(.vectors))
  } > FLASH

  /* Linker-synthesized SG veneers land here (--cmse-implib); veneer bodies
   * live in .text. The dot assignment keeps the section address assigned even
   * when no input object carries .gnu.sgstubs content, or ld cannot size the
   * synthesized stubs ("no address assigned to the veneers output section"). */
  .gnu.sgstubs (WT_SECURE_FLASH_ORIGIN + WT_SECURE_IMAGE_HEADER_SIZE + 0x400) : {
    *(.gnu.sgstubs)
    *(.gnu.sgstubs*)
    . = ALIGN(32);
  } > FLASH

  .nsc ALIGN(32) : {
    KEEP(*(.nsc*))
  } > FLASH

  .text (WT_SECURE_FLASH_ORIGIN + WT_SECURE_IMAGE_HEADER_SIZE + 0x800) : {
    *(.text*)
    *(.rodata*)
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
  } > FLASH

  ASSERT(WT_SECURE_IMAGE_HEADER_SIZE < WT_SECURE_FLASH_SIZE,
         "secure image header consumes secure flash region")
  ASSERT(. <= (WT_SECURE_FLASH_ORIGIN + WT_SECURE_FLASH_SIZE),
         "secure image overflows secure flash region")
  ASSERT(SIZEOF(.gnu.sgstubs) + SIZEOF(.nsc) <= 0x400, "NSC window overflow")

  /* Claim the conformance partitions' .data/.bss before the general rules so
   * they land in CONFDATA, not shared SPM RAM. Loaded from flash and copied by
   * Reset_Handler alongside .data; empty (zero size) in non-conformance builds. */
  /* Per-partition private data bands (Arm i080/i084 SP data isolation): the
   * server partition's test data and the driver partition's data each get a
   * 32-byte-aligned band the SVC gate denies to every other partition. Their
   * .bss lands in the loaded section so each band stays one contiguous MPU
   * carve; the flash cost is a few zero words. */
  .conf_data : {
    . = ALIGN(4);
    _sconfdata = .;
    EXCLUDE_FILE(*conf_sec_test_supp_*.o *conf_sec_driver_partition.o) *conf_sec_*.o(.data .data.*)
    . = ALIGN(32);
    _s_conf_server_data = .;
    *conf_sec_test_supp_*.o(.data .data.* .bss .bss.* COMMON)
    . = ALIGN(32);
    _e_conf_server_data = .;
    _s_conf_driver_data = .;
    *conf_sec_driver_partition.o(.data .data.* .bss .bss.* COMMON)
    . = ALIGN(32);
    _e_conf_driver_data = .;
    _econfdata = .;
  } > CONFDATA AT > FLASH
  _siconfdata = LOADADDR(.conf_data);

  .conf_bss (NOLOAD) : {
    . = ALIGN(4);
    _sconfbss = .;
    EXCLUDE_FILE(*conf_sec_test_supp_*.o *conf_sec_driver_partition.o) *conf_sec_*.o(.bss .bss.* COMMON)
    . = ALIGN(4);
    _econfbss = .;
  } > CONFDATA

  .data : {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) : {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM

  .sp_stacks (NOLOAD) : {
    KEEP(*(.sp_stacks*))
  } > SPSTACKS

  ASSERT(ORIGIN(VAULTSTACK) == (ORIGIN(RAM) + LENGTH(RAM)),
         "vault stack must sit directly above the secure RAM window")
  ASSERT(ORIGIN(CONFDATA) == (ORIGIN(VAULTSTACK) + LENGTH(VAULTSTACK)),
         "conformance SP data must sit directly above the vault stack")
  /* The top 1 KiB of CONFDATA holds the per-partition pseudo-MMIO holes
   * (WT_CONF_SERVER/DRV_MMIO in memory_map.h); real data crossing into them
   * would be carved out of other partitions' domains and fault at runtime. */
  ASSERT(_econfbss <= (ORIGIN(CONFDATA) + 0x2C00),
         "conformance data/bss overflows into the reserved MMIO holes")
  ASSERT(ORIGIN(SPSTACKS) == (ORIGIN(CONFDATA) + LENGTH(CONFDATA)),
         "SP secure stacks must sit directly above the conformance data window")
}
