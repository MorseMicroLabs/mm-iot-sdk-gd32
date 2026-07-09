/*
 * SUPERSEDED - not used by the build.
 *
 * This linker script was written for a hypothetical "GD32F470ZK" part (3MB flash) before the real
 * dev board and schematic were available. The real board turned out to be a GD32F470VGT6
 * (1024KB/1MB flash, LQFP100), so this platform now uses
 * framework/src/platforms/mm-ekh08-gd32f470/bsp/GD32F470VG_FLASH.ld instead (referenced from
 * framework/mk/platform-mm-ekh08-gd32f470.mk via BSP_LD_PREFIX).
 *
 * This file is left in place only because files in the workspace folder cannot be deleted; it is
 * not compiled or referenced by anything.
 *
 * Original header follows, for reference:
 *
 * Linker script for GD32F470xK (3MB flash / 192KB SRAM + 64KB TCMRAM).
 *
 * Based directly on GigaDevice's official
 * GD32F4xx_Firmware_Library_V3.3.3/Firmware/CMSIS/GD/GD32F4xx/Source/GCC/Ld/gd32f470xK_flash.ld
 * (see framework/src/bsps/gd32f4xx/Drivers/CMSIS/GD/GD32F4xx/Source/gd32f470xK_flash.ld for the
 * unmodified original), with the FLASH region split into BOOTLOADER/CONFIG/FLASH to make room for
 * the mmconfig persistent-configuration partition, matching the pattern used by the other
 * mm-ekh08-* platforms (see e.g. framework/src/platforms/mm-ekh08-h753/bsp/STM32H753ZITX_FLASH.ld).
 *
 * BOOTLOADER (sectors 0-4, 128K) and CONFIG (sectors 5-6, 256K) are sized to land exactly on
 * flash sector boundaries for GD32F470xK's bank 0 (sectors 0-3: 16K each, sector 4: 64K,
 * sectors 5-11: 128K each -- see
 * framework/src/bsps/gd32f4xx/Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_fmc.h
 * CTL_SECTOR_NUMBER_* / CTL_SN() for the sector-to-address mapping used by mm_shims/mmhal_flash.c).
 *
 * TCMRAM is declared (matching the real part) but unused by this platform for now.
 */

ENTRY(Reset_Handler)

MEMORY
{
  BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 128K
  CONFIG     (r)  : ORIGIN = 0x08020000, LENGTH = 256K
  FLASH      (rx) : ORIGIN = 0x08060000, LENGTH = 2688K
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
