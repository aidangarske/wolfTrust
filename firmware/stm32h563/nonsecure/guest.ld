ENTRY(Reset_Handler)

MEMORY {
  FLASH (rx) : ORIGIN = GUEST_FLASH_ORIGIN, LENGTH = 64K
  RAM   (rwx): ORIGIN = GUEST_RAM_ORIGIN, LENGTH = 16K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sidata = LOADADDR(.data);

SECTIONS {
  .shared ORIGIN(RAM) (NOLOAD) : {
    *(.shared*)
  } > RAM

  .vectors GUEST_FLASH_ORIGIN : {
    KEEP(*(.vectors))
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
