/*
 * Linker script for GD32F470VGT6 (1024KB/1MB flash / 192KB SRAM + 64KB TCMRAM).
 *
 * The real dev board used for this port is a GD32F470VGT6 "core board" (LQFP100 package),
 * confirmed from the vendor package supplied with the board: schematic
 * "SCH_LQFP100-14_14-V03_ARM_25M_2024-07-08.pdf", the GD32F470xx datasheet (Table 2-1: the "VG"
 * density is 1024KB total flash -- 768KB "code area" + 256KB "data area", both part of the same
 * physically contiguous, uniformly-erasable bank), and the GD32F4xx User Manual (Table 2-1: flash
 * "bank 0" is always exactly 1MB for any part with >=1MB total flash; the DBS option byte is
 * documented specifically for "when flash size is 1MB", confirming 1MB is a real, standard GD32F4xx
 * density, not a doc error).
 *
 * NOTE: GigaDevice's own default GCC linker script for this density,
 * GD32F4xx_Firmware_Library_V3.3.3/Firmware/CMSIS/GD/GD32F4xx/Source/GCC/Ld/gd32f470xG_flash.ld
 * (see framework/src/bsps/gd32f4xx/Drivers/CMSIS/GD/GD32F4xx/Source/GCC/Ld/gd32f470xG_flash.ld for
 * the unmodified original), sets FLASH LENGTH = 512K -- this appears to be a copy-paste leftover
 * from the smaller "E" (512KB) density template (the xE and xG default scripts are byte-identical
 * apart from the filename) rather than a deliberate choice, and contradicts both the datasheet and
 * user manual. This linker script uses the datasheet/user-manual figure (1024KB) instead, which was
 * confirmed as correct once building the real firmware showed ~520KB of code+rodata was needed --
 * far more than 512KB would ever allow even without any bootloader/config reservation, but well
 * within 1024KB.
 *
 * The FLASH region is split into BOOTLOADER/CONFIG/FLASH to make room for the mmconfig
 * persistent-configuration partition. Sizing follows the convention used by most mm-ekh08-*
 * platforms (see e.g. framework/src/platforms/mm-ekh08-wb55/bsp/stm32wb55xx_flash_cm4.ld,
 * mm-ekh08-u575/bsp/STM32U575ZITXQ_FLASH.ld, mm-mm6108-ekh05/bsp/STM32U585XX_FLASH.ld -- all use
 * BOOTLOADER=16K/CONFIG=16K at the same 0x08000000/0x08004000 addresses), not the oversized
 * 128K/256K split mm-ekh08-h753 uses (that chip's smallest flash sector happens to be 128K, so it
 * has no choice; GD32F470's smallest sectors are 16K, same as the STM32 parts above, so the same
 * compact 16K/16K split applies here):
 *
 *   BOOTLOADER: sector 0  (16K),               0x08000000-0x08003FFF
 *   CONFIG:     sector 1  (16K),                0x08004000-0x08007FFF
 *   FLASH:      sectors 2-11 (2x16K + 1x64K + 7x128K = 992K), 0x08008000-0x080FFFFF
 *
 * This sector map (4x16K, 1x64K, then 128K sectors up to sector 11 at the 1MB boundary) is the
 * standard GD32F4xx/STM32F4 bank-0 layout and is confirmed directly against the real
 * framework/src/bsps/gd32f4xx/Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_fmc.h
 * (CTL_SECTOR_NUMBER_0..11); see mm_shims/mmhal_flash.c. A 16K BOOTLOADER/CONFIG only leaves room
 * for a minimal bootloader stub and a single-sector (16K) mmconfig store; if you need more room for
 * either, note that the next sector up is 64K (sector 2), not another 16K sector.
 *
 * TCMRAM is declared (matching the real part) but unused by this platform for now.
 */

ENTRY(Reset_Handler)

MEMORY
{
  BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 16K
  CONFIG     (r)  : ORIGIN = 0x08004000, LENGTH = 16K
  FLASH      (rx) : ORIGIN = 0x08008000, LENGTH = 992K
  RAM        (xrw): ORIGIN = 0x20000000, LENGTH = 192K
  TCMRAM     (xrw): ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  __stack_size = DEFINED(__stack_size) ? __stack_size : 2K;
  __heap_size = DEFINED(__heap_size) ? __heap_size : 1K;

  /* ISR vectors */
  .vectors :
  {
    . = ALIGN(4);
    KEEP(*(.vectors))
    . = ALIGN(4);
    __Vectors_End = .;
    __Vectors_Size = __Vectors_End - __gVectors;
  } >FLASH

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

  /* Reserve this region for the bootloader; for now we just have a bootstrap stub here. */
  .bootloader :
  {
    KEEP(*(.bootloader))
  } >BOOTLOADER

  /* text section, such as program code */
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

  /* ro-data section, such as constant data */
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

  /* provide some necessary symbols for initialized data */
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

  /* provide some necessary symbols for uninitialized data */
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

  .noinit :
  {
    *(.noinit)
    *(.noinit.*)
    . = ALIGN(4);
  } > RAM

  /* heap and stack space -- _sp is loaded as the initial MSP from vectors[0] on reset. */
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

  /* Note: to use TCMRAM, initialization in the startup file would be required; unused for now. */
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

  /* Force use of the nano-libc (see CSPECS in framework/mk/core-arm-cortex-m4f.mk) rather than
   * accidentally pulling in the standard libc/libm/libgcc archives. */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}
