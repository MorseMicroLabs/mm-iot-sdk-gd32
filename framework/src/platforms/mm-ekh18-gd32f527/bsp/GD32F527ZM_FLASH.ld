/*
 * Linker script for GD32F527ZMT7 (4MB flash; 256KB SRAM mapped here + 64KB TCMRAM).
 *
 * NOTE: an earlier version of this header claimed "1MB SRAM", which contradicted the 256K RAM
 * entry in the MEMORY block below. Only the 256KB actually mapped here has been used and tested;
 * confirm against the GD32F527xx datasheet before relying on more.
 *
 * GD32F527ZMT7 has 4096KB of flash in two 2048KB banks. Confirmed on silicon by reading over the
 * ROM bootloader: reads succeed up to 0x083FFFF0 and are refused from 0x08400000 onwards. This
 * matches GigaDevice's own GD32MCUFlashXML/GD32F527.xml (GD32F527XMXX: 4096KB, 40 pages).
 *
 *   Bank 0: 0x08000000-0x081FFFFF   pages 0-3 = 16K, page 4 = 64K, pages 5-19 = 128K
 *   Bank 1: 0x08200000-0x083FFFFF   pages 20-23 = 16K, page 24 = 64K, pages 25-39 = 128K
 *
 * The application deliberately lives entirely within BANK 0, leaving the whole of bank 1 free:
 *
 *   BOOTLOADER: page 0      (16K),   0x08000000-0x08003FFF
 *   CONFIG:     pages 1-2   (32K),   0x08004000-0x0800BFFF
 *   FLASH:      pages 3-19  (2000K), 0x0800C000-0x081FFFFF
 *   (reserved)  pages 20-39 (2048K), 0x08200000-0x083FFFFF  -- bank 1, for future OTA
 *
 * Keeping the image inside one bank leaves the door open for an A/B OTA scheme: a new image can
 * be written into bank 1 while running from bank 0, then activated by flipping the BB (boot bank
 * select) option bit rather than overwriting the running image. Do not spill the application into
 * bank 1 without revisiting that plan.
 *
 * Earlier revisions of this file mapped only 976KB, which was an unexamined copy of the GD32F470
 * layout (that part genuinely has just 1MB). ap_mode reached 89.8% of that region.
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
  /* Rest of bank 0. Bank 1 (0x08200000, 2048K) is deliberately left unmapped and unused so it
   * stays available for an A/B OTA image -- see the header comment. */
  FLASH      (rx) : ORIGIN = 0x0800C000, LENGTH = 2000K
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
