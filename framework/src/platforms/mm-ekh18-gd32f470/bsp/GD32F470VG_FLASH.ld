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
 * persistent-configuration partition:
 *
 *   BOOTLOADER: sector 0    (16K),                            0x08000000-0x08003FFF
 *   CONFIG:     sectors 1-2 (32K: 2x16K),                      0x08004000-0x0800BFFF
 *   FLASH:      sectors 3-11 (1x16K + 1x64K + 7x128K = 976K),  0x0800C000-0x080FFFFF
 *
 * CONFIG is 32K (two 16K sectors), NOT the 16K/single-sector convention most mm-ekh08-* platforms
 * use (see e.g. framework/src/platforms/mm-ekh08-wb55/bsp/stm32wb55xx_flash_cm4.ld,
 * mm-ekh08-u575/bsp/STM32U575ZITXQ_FLASH.ld -- both 16K CONFIG). Those platforms' *physical* flash
 * sector size (4K for wb55, 8K for u575) is smaller than their 16K CONFIG region, so mmconfig's
 * two software partitions (each half of CONFIG) still land on independently-erasable physical
 * sector boundaries. GD32F470's smallest sector is 16K -- the same size as a single-sector CONFIG
 * region -- so a 16K CONFIG could not be split into two independently-erasable partitions at all:
 * mmconfig_erase_partition() in framework/src/mmconfig/mmconfig.c erases in
 * mmhal_flash_getblocksize() (16K) chunks, so erasing an 8K partition would have actually erased
 * the full 16K sector, wiping out the *other* partition too and defeating mmconfig's power-loss
 * redundancy. Doubling CONFIG to 32K (sectors 1-2, one physical 16K sector per software partition)
 * fixes this -- see mm_shims/mmhal_flash.c. This costs 16K of application flash versus the
 * single-sector convention (976K available instead of 992K).
 *
 * This sector map (4x16K, 1x64K, then 128K sectors up to sector 11 at the 1MB boundary) is the
 * standard GD32F4xx/STM32F4 bank-0 layout and is confirmed directly against the real
 * framework/src/bsps/gd32f4xx/Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_fmc.h
 * (CTL_SECTOR_NUMBER_0..11); see mm_shims/mmhal_flash.c. If you need more room for BOOTLOADER or
 * CONFIG, note that the next sector up after sector 3 is 64K (sector 4), not another 16K sector.
 *
 * IMPORTANT -- .vectors is placed in BOOTLOADER (0x08000000), NOT FLASH, UNLIKE the sibling
 * mm-ekh08-* and mm-mm8108-ekh05 platforms (compare e.g.
 * framework/src/platforms/mm-ekh08-wb55/bsp/stm32wb55xx_flash_cm4.ld, whose `.isr_vector` is
 * placed `>FLASH`, i.e. at its FLASH region's origin, past its own BOOTLOADER/CONFIG split). Those
 * platforms' BOOTLOADER region is reserved for a real, separately-flashed first-stage bootloader
 * that is expected to already be resident (e.g. pre-provisioned on the reference dev board) and
 * responsible for jumping into the app at FLASH's origin -- but no such bootloader exists anywhere
 * in this SDK for any platform (there is no bootloader source/build target in this repo at all),
 * and this platform's bare third-party dev board does not ship with one either. On a Cortex-M4 in
 * "boot from main flash" mode (the normal default, confirmed by the fact that the board's factory
 * demo firmware runs at all), the CPU unconditionally fetches its initial SP from 0x08000000 and
 * initial PC from 0x08000004 in hardware at cold reset -- this is fixed silicon behaviour, not
 * affected by software or by VTOR. If `.vectors` is placed anywhere else (e.g. at FLASH's origin,
 * 0x0800C000, as originally done here), `program ... verify reset exit` in `make reprogram` never
 * writes to 0x08000000 at all (nothing in the ELF targets that address), so a hardware reset just
 * keeps running whatever was already there -- which is exactly why this platform's app never ran
 * on real hardware even though flashing reported success: the original factory demo's vector table
 * at 0x08000000 was left completely untouched. Separately, GigaDevice's own vendored
 * `system_gd32f4xx.c`:`SystemInit()` unconditionally calls
 * `nvic_vector_table_set(NVIC_VECTTAB_FLASH, VECT_TAB_OFFSET)` (`NVIC_VECTTAB_FLASH` ==
 * 0x08000000, `VECT_TAB_OFFSET` == 0), i.e. it forcibly sets VTOR to 0x08000000 every boot
 * regardless of where `.vectors` was linked -- so even if a hypothetical bootloader got us to
 * Reset_Handler with a different VTOR already set, this call would immediately break interrupt
 * routing (EXTI, USART, etc.) unless `.vectors` genuinely lives at 0x08000000. Both problems are
 * fixed simply by placing `.vectors` in BOOTLOADER instead of FLASH: the vector table (a few
 * hundred bytes) fits trivially within BOOTLOADER's 16K, `.text`/`.rodata`/etc. are unaffected
 * (still in FLASH at 0x0800C000+, since the vector table only needs to *contain the address* of
 * Reset_Handler, not be adjacent to it), and both the hardware boot fetch and SystemInit()'s VTOR
 * write now correctly land on this platform's real vector table. If a genuine bootloader is added
 * to this platform in the future, this will need to be revisited (the bootloader would become the
 * true occupant of 0x08000000, and this app's vector table would move back out to FLASH along with
 * an explicit VTOR relocation in the bootloader before it jumps to the app).
 *
 * TCMRAM is declared (matching the real part) but unused by this platform for now.
 */

ENTRY(Reset_Handler)

MEMORY
{
  BOOTLOADER (rx) : ORIGIN = 0x08000000, LENGTH = 16K
  CONFIG     (r)  : ORIGIN = 0x08004000, LENGTH = 32K
  FLASH      (rx) : ORIGIN = 0x0800C000, LENGTH = 976K
  RAM        (xrw): ORIGIN = 0x20000000, LENGTH = 192K
  TCMRAM     (xrw): ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  __stack_size = DEFINED(__stack_size) ? __stack_size : 2K;
  __heap_size = DEFINED(__heap_size) ? __heap_size : 1K;

  /* ISR vectors -- placed in BOOTLOADER (0x08000000), the true hardware boot vector address, not
   * FLASH. See the file header comment for why this platform intentionally diverges from the
   * sibling mm-ekh08-* and mm-mm8108-ekh05 platforms' convention of placing vectors at FLASH's origin
   * (past their BOOTLOADER/CONFIG split), which assumes a real bootloader occupies BOOTLOADER --
   * this platform has none. */
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

  /* Nothing currently contributes a `.bootloader`-tagged section (no real bootloader exists for
   * this platform -- see the file header comment); this is a no-op placeholder that reserves room
   * in BOOTLOADER (alongside .vectors above) for a possible future bootloader stage. */
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
