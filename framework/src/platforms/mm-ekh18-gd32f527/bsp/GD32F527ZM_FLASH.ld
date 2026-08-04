/*
 * Linker script for GD32F527ZMT7 (4MB flash / 1MB SRAM + 64KB TCMRAM).
 *
 * GD32F527ZMT7: 4096KB total flash in bank 0 (sectors 0-3=16K, sector 4=64K, sectors 5-11=128K
 * then sectors 12+ for the extra capacity). For the mmconfig partition we only use the first
 * 1MB (sectors 0-11), identical layout to GD32F470:
 *
 *   BOOTLOADER: sector 0     (16K),  0x08000000-0x08003FFF
 *   CONFIG:     sectors 1-2  (32K),  0x08004000-0x0800BFFF
 *   FLASH:      sectors 3-11 (976K), 0x0800C000-0x080FFFFF
 *
 * (The remaining ~3MB of flash at 0x08100000+ is available for future use but unused here.)
 *
 * RAM:    256KB @ 0x20000000 (F527 has 256KB vs F470's 192KB)
 * TCMRAM:  64KB @ 0x10000000
 *
 * IMPORTANT: .vectors is placed in BOOTLOADER (0x08000000), the real CPU boot address.
 * See mm-ekh18-gd32f470/bsp/GD32F470VG_FLASH.ld for a detailed explanation of why.
 */

ENTRY(Reset_Handler)

MEMORY
{
  BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 16K
  CONFIG     (r)  : ORIGIN = 0x08004000, LENGTH = 32K
  FLASH      (rx) : ORIGIN = 0x0800C000, LENGTH = 976K
  RAM        (xrw): ORIGIN = 0x20000000, LENGTH = 256K
  TCMRAM     (xrw): ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  __stack_size = DEFINED(__stack_size) ? __stack_size : 2K;
  __heap_size = DEFINED(__heap_size) ? __heap_size : 1K;

  /* ISR vectors at BOOTLOADER (0x08000000), the true hardware boot vector address. */
  .vectors :
  {
    . = ALIGN(4);
    KEEP(*(.vectors))
    . = ALIGN(4);
    __Vectors_End = .;
    __Vectors_Size = __Vectors_End - __gVectors;
  } >BOOTLOADER

  .mmconfig :
  {
    mmconfig_start = ORIGIN(CONFIG);
    mmconfig_end   = ORIGIN(CONFIG) + LENGTH(CONFIG);
  }

  PROVIDE (_emmet_hostif_table_base_address = 0x20000400);

  .emmet_hostif_table _emmet_hostif_table_base_address (NOLOAD) :
  {
    *(.emmet_hostif_table)
  } > RAM

  .bootloader :
  {
    KEEP(*(.bootloader))
  } >BOOTLOADER

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .ARM.attributes :
  {
    *(.ARM.attributes)
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(.fini_array*))
    KEEP (*(SORT(.fini_array.*)))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > RAM

  /* GD32F5xx SRAM is ECC-protected, and .noinit is deliberately skipped by the startup zero-fill
   * loop (which stops at _ebss). On a cold boot its ECC syndrome is undefined, so the first read
   * of it raises an uncorrectable ECC error -> NMI. ECC_PrimeNoinit() in bsp/Core/Src/main.c
   * primes this range on power-on reset; these two symbols delimit it. */
  .noinit :
  {
    _snoinit = .;
    *(.noinit)
    *(.noinit.*)
    . = ALIGN(4);
    _enoinit = .;
  } > RAM

  .heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = _ebss );
    PROVIDE ( _end = _ebss );
    . = . + __heap_size;
    PROVIDE( _heap_end = . );
    . = . + __stack_size;
    PROVIDE( _sp = . );
    . = ALIGN(8);
  } > RAM

  _sitcmram = LOADADDR(.tcmram);
  .tcmram :
  {
    . = ALIGN(4);
    _stcmram = .;
    *(.tcmram)
    *(.tcmram*)
    . = ALIGN(4);
    _etcmram = .;
  } > TCMRAM AT> FLASH

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}
