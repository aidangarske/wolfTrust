ENTRY(Reset_Handler)

MEMORY {
  FLASH (rx) : ORIGIN = 0x0C000000, LENGTH = 128K
  RAM   (rwx): ORIGIN = 0x30020000, LENGTH = 512K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sidata = LOADADDR(.data);

SECTIONS {
  .vectors 0x0C000000 : {
    KEEP(*(.vectors))
  } > FLASH

  .gnu.sgstubs 0x0C000400 : {
    *(.gnu.sgstubs)
    *(.gnu.sgstubs*)
  } > FLASH

  .nsc ALIGN(32) : {
    KEEP(*(.nsc*))
  } > FLASH

  .text 0x0C000800 : {
    *(.text*)
    *(.rodata*)
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
  } > FLASH

  ASSERT(. <= 0x0C002000, "secure image overlaps guest image")
  ASSERT(SIZEOF(.gnu.sgstubs) + SIZEOF(.nsc) <= 0x400, "NSC window overflow")

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
