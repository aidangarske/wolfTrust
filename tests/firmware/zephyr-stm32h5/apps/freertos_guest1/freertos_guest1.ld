/* Linker script for the wolfTrust FreeRTOS NS guest. Same layout as the
 * baremetal heartbeat guest1: vector table at the start of flash, .reset
 * immediately after, then .text / .rodata in flash, .data / .bss in RAM
 * past the 0x100..0x300 HSM transport CSR reservation. */

ENTRY(Reset_Handler)

MEMORY {
  FLASH (rx) : ORIGIN = GUEST_FLASH_ORIGIN, LENGTH = GUEST_FLASH_LENGTH
  RAM   (rwx): ORIGIN = GUEST_RAM_ORIGIN,   LENGTH = GUEST_RAM_LENGTH
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sidata = LOADADDR(.data);

SECTIONS {
  /* Mirror baremetal guest1.ld: keep ORIGIN+0x100..+0x2FF clear so the
   * wolfHSM transport CSR (defined absolutely at WT_GUEST1_HSM_BUF_BASE)
   * doesn't collide with anything the FreeRTOS heap/.data/.bss places. */
  .hsm_transport_reserved (ORIGIN(RAM) + 0x100) (NOLOAD) : {
    . += 0x200;
  } > RAM

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
