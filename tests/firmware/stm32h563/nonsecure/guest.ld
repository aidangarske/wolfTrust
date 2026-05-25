/* guest.ld
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
  FLASH (rx) : ORIGIN = GUEST_FLASH_ORIGIN, LENGTH = GUEST_FLASH_LENGTH
  RAM   (rwx): ORIGIN = GUEST_RAM_ORIGIN, LENGTH = GUEST_RAM_LENGTH
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sidata = LOADADDR(.data);

SECTIONS {
  .shared ORIGIN(RAM) (NOLOAD) : {
    *(.shared*)
  } > RAM

  /* wolfHSM CMSE transport buffer: 512 B at GUEST_RAM_ORIGIN + 0x100.
   * NOLOAD — the secure side initialises this region via cmse_check_address_range.
   * Matches WT_HSM_BUF_OFFSET / WT_HSM_BUF_SIZE in the STM32H563 port map. */
  .hsm_transport (ORIGIN(RAM) + 0x100) (NOLOAD) : {
    _hsm_transport_base = .;
    . += 0x200;
    _hsm_transport_end = .;
  } > RAM

  _hsm_transport_size = _hsm_transport_end - _hsm_transport_base;

  .vectors GUEST_FLASH_ORIGIN : {
    KEEP(*(.vectors))
  } > FLASH

  .reset : {
    KEEP(*(.reset))
  } > FLASH

  .text : {
    *(.text*)
    *(.rodata*)
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
  } > FLASH

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
}
