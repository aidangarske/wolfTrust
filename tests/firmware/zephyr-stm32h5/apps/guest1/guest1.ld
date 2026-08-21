/* guest1.ld — linker script for the baremetal heartbeat companion.
 * Mirrors tests/firmware/stm32h563/nonsecure/guest.ld so the layout
 * stays compatible with the wolfTrust guest-b partition: the first
 * 0x300 bytes of RAM are kept clear for the CMSE/HSM transport window
 * that the secure side reserves, even though this guest never uses it. */

ENTRY(Reset_Handler)

MEMORY {
  FLASH (rx) : ORIGIN = GUEST_FLASH_ORIGIN, LENGTH = GUEST_FLASH_LENGTH
  RAM   (rwx): ORIGIN = GUEST_RAM_ORIGIN,   LENGTH = GUEST_RAM_LENGTH
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sidata = LOADADDR(.data);

SECTIONS {
  .hsm_transport_reserved (ORIGIN(RAM) + 0x100) (NOLOAD) : {
    . += 0x300;
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
