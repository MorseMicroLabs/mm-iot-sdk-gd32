mm-ekh18-gd32f470 Readme {#MM_EKH18_GD32F470_README}
====

__Copyright 2026 Morse Micro__

# Summary

This directory contains the hardware-dependent code needed to use a GD32F470VGT6 (LQFP100, 1024KB
flash) core board with the MM8108 (MMECH18 module).

__Note:__ this platform was previously developed under the name `mm-ekh08-gd32f470` (it was
originally built against the wrong chip, MM6108/MMECH08, before being corrected to MM8108/MMECH18).
Once the chip was corrected, the directory itself was renamed to `mm-ekh18-gd32f470` to match. The
old `mm-ekh08-gd32f470` directory still exists (this workspace does not allow deleting or renaming
files, only overwriting content) but is now superseded -- see its readme.md for a pointer here. Any
build configuration referencing `mm-ekh08-gd32f470`/`platform-mm-ekh08-gd32f470.mk` will
transparently forward to this platform.

`../../bsps/gd32f4xx/Drivers` (CMSIS device support and the `GD32F4xx_standard_peripheral` drivers)
is vendored **verbatim** from GigaDevice's official `GD32F4xx_Firmware_Library_V3.3.3`, the same
way `../../bsps/stm32cubeh7`/`stm32cubewb`/`stm32cubeu5` are vendored unmodified for the STM32
platforms -- see `../../bsps/gd32f4xx/README-PROVENANCE.md` for details. `bsp/Core/Src/main.c` and
the `mm_shims/*.c` files were written/updated against this real API.

The pin mapping, crystal value, and flash/RAM sizes below are all confirmed against the real
schematic packaged with the dev board itself (`SCH_LQFP100-14_14-V03_ARM_25M_2024-07-08.pdf`) and
the accompanying GD32F470xx datasheet and GD32F4xx User Manual -- they are no longer placeholders.
The dev board is a bare GPIO breakout ("core board"): every MCU pin is broken out sequentially on a
50-pin header ("H2"), not an Arduino-shield-compatible layout, so the MMECH18 module must be wired
to it with jumper wires rather than plugged in directly.

**Note on flash size:** GigaDevice's own default GCC linker script for the "VG" density
(`gd32f470xG_flash.ld`) states 512KB flash, but this is contradicted by both the datasheet (Table
2-1: "VG" = 1024KB total) and the User Manual (flash bank 0 is always exactly 1MB for any part with
>=1MB flash, and the DBS option byte is documented specifically for "1MB flash" parts) -- the
default script appears to be a copy-paste leftover from the smaller "E" (512KB) density template.
This was confirmed in practice: a real build of `porting_assistant` needs ~520KB of flash, which
would be impossible to explain if the part only had 512KB total. This platform uses the
datasheet/user-manual figure (1024KB).

__READ THIS BEFORE USING ON REAL HARDWARE.__ This platform builds and links successfully with a
real `arm-none-eabi-gcc` toolchain (`porting_assistant` example), and has been flashed to and run
on real GD32F470VGT6 + MMECH18 hardware, reaching BCF validation in `porting_assistant`. Four real
bugs were found and fixed this way (none reproducible without a physical board):

1. The vector table was linked into the wrong flash region, so a hardware reset just kept booting
   the board's original factory demo -- see the BOOTLOADER/vector-table bullet below.
2. `SystemClock_Config()` in `bsp/Core/Src/main.c` compared the system clock source *readback*
   against the wrong constant (`RCU_CKSYSSRC_PLLP`, meant only for *requesting* PLLP as the clock
   source, instead of `RCU_SCSS_PLLP`, the correct constant for checking what was actually
   selected -- these occupy different bit positions in the same register). This meant
   `Error_Handler()` was hit on every single boot immediately after clock setup, even though HXTAL
   and the PLL were both configuring and locking correctly -- confirmed via live GDB register
   inspection, not just code review. This is now fixed.
3. The MM8108 BUSY line's EXTI trigger was configured `EXTI_TRIG_BOTH` (fires on rising AND
   falling edges) in both `bsp/Core/Src/main.c` and `mm_shims/mmhal_wlan.c`, when it should be
   `EXTI_TRIG_RISING` only -- this doubled the observed IRQ count in `porting_assistant`'s "Verify
   BUSY pin" test. Fixed in both places.
4. `mm_shims/mmhal_flash.c`'s `fmc_bank0_sector_of_address()` matched flash addresses via exact
   equality against each sector's *start* address only, instead of a `[start, start+size)` range
   check. `mmconfig_init()` probes the *last* byte of the CONFIG partition, which generally isn't a
   sector's first byte, so this always failed and crashed the "Validate BCF" test step via
   `MMOSAL_ASSERT`. Fixed to do a proper range lookup -- see README-PROVENANCE.md for details.

Beyond those four fixes, this port is still otherwise unverified on real hardware end-to-end
(full WLAN connect/data-path, sleep, etc.). The following remain unverified/simplified:

- MCU deep sleep (GD32F4xx low-power modes) is **not implemented**; only WFI-based shallow sleep
  is supported (see `mm_shims/mmhal_os.c`).
- SPI transfers to the MM8108 are polled only (no DMA) -- see `mm_shims/mmhal_wlan.c`.
- There is no on-chip RTC in use; `mmhal_get_time()`/`mmhal_set_time()` use a software clock
  derived from the RTOS tick (see `mm_shims/mmhal_app.c`), which resets to the epoch every power
  cycle unless something (e.g. SNTP) calls `mmhal_set_time()` after boot.
- Buttons and debug GPIOs are stubbed out (no-ops): the board's only button (SW1) is wired directly
  to the hardware NRST pin, not a GPIO, and no debug pins are broken out. LEDs, however, *are*
  implemented (see below).
- `openocd.cfg` targets the board's onboard DAPLink probe (CMSIS-DAP over SWD, via OpenOCD's stock
  `interface/cmsis-dap.cfg`) combined with OpenOCD's stock `target/stm32f4x.cfg`, since there is no
  GD32F4xx-specific target script in mainline OpenOCD; verify flash programming works or use
  GigaDevice's own programming tool (the board's vendor package also includes a UART ISP tool as
  an alternative to SWD).
- 976K of the 1024K flash is available to the application; 16K is reserved for BOOTLOADER (see
  below) and 32K for the mmconfig CONFIG partition (see `bsp/GD32F470VG_FLASH.ld`). CONFIG is 32K
  here rather than the 16K used by most mm-ekh08-*/mm-ekh18-*/mm-mm6108-ekh05/mm-mm8108-ekh05
  platforms: those platforms' physical flash sectors are smaller than 16K, so a 16K CONFIG region
  still splits into two independently-erasable sectors for mmconfig's A/B redundant storage.
  GD32F470's smallest sector is 16K itself, so CONFIG needs two whole 16K sectors (32K total) to
  give mmconfig's two software partitions their own erasable sector each -- a single 16K CONFIG
  sector would have let erasing one partition spill into and corrupt the other. If you need more
  room for either partition, note the next sector up is 64K, not another 16K sector.
- **This platform's own vector table lives in the BOOTLOADER region (0x08000000), NOT FLASH**,
  unlike sibling mm-ekh08-*/mm-mm8108-ekh05 platforms. There is no real bootloader implemented
  anywhere in this SDK; those other platforms' `.vectors`-in-FLASH convention assumes one is
  separately provisioned at 0x08000000 to hand off to the app, which is not the case for this bare
  third-party dev board. Cortex-M4 unconditionally fetches its initial SP/PC from 0x08000000 at
  cold reset (fixed in hardware, not software-configurable), so the app's own vector table has to
  physically be there for the board to boot into it at all -- see the header comment in
  `bsp/GD32F470VG_FLASH.ld` for the full explanation (including why GigaDevice's vendored
  `SystemInit()` would have broken interrupt routing even if boot itself somehow worked around
  this). `.text`/`.rodata`/etc. are unaffected and still live in FLASH.
- On disable (`mmhal_wlan_deinit()`), the SPI peripheral and all MM8108 control GPIOs are fully
  de-initialised (clock disabled, pins set to safe output-low) to prevent current leakage into a
  powered-down chip, matching the pattern used by `mm-mm8108-ekh05` -- see `mm_shims/mmhal_wlan.c`.

If you are using a *different* GD32F470 board than the one described here, re-verify the pin
mapping in `bsp/Core/Inc/main.h`, the crystal/clock tree in `bsp/Core/Src/main.c`, and the flash
layout in `bsp/GD32F470VG_FLASH.ld` against your own schematic before use.

The `bsp` directory contains the startup file, linker script, and top-level clock/peripheral
bring-up code (`Core/Src/main.c`). The files in the `mm_shims` directory (e.g. `mmhal_core.c`,
`mmport.h`, `mmhal_wlan.c`) are where the board-specific API functions used by morselib and other
software components are implemented.

## Pinout MMECH18 (confirmed against the real schematic)

The dev board's GPIO breakout header is called "H2" on the schematic; H2 pin numbers are given
below for reference when wiring jumpers. RESET_N/WAKE/BUSY/SPI_IRQ were chosen from H2's free,
unused pins (i.e. not already used by the board's own USART0-TTL breakout or onboard LEDs).

MM8108 pin | GD32F470VGT6 pin | H2 pin
-----------|------------------|-------
RESET_N    | PC4              | 9
WAKE       | PC5              | 10
BUSY       | PB0              | 11
SPI_IRQ    | PB1              | 12
SPI_CS     | PA4              | 5
SPI_SCK    | PA5              | 6
SPI_MISO   | PA6              | 7
SPI_MOSI   | PA7              | 8

SPI0's default (non-remapped) pins happen to land on four consecutive H2 pins (5-8), which is why
they were chosen for the MM8108's SPI bus.

Log console (not part of MMECH18): USART0 on PA9 (TX, H2 pin 48) / PA10 (RX, H2 pin 46),
115200 8N1. These are the board's own TTL serial breakout pins (silkscreen-labelled on the
schematic itself), not extra wiring.

Ground: H2 pins 15, 49, 50 (or any other H2 GND pin).

## Onboard LEDs

The board has three GPIO-controllable LEDs (plus one always-on power LED that is not
GPIO-controlled), all wired VCC -> LED -> resistor -> GPIO (active-LOW):

Colour  | Pin | mmhal usage
--------|-----|------------
Green   | PA0 | `LED_GREEN`
Blue    | PA1 | `LED_BLUE`
Yellow  | PA2 | error LED (`mmhal_set_error_led()`)

There is no GPIO-controllable red or white LED on this board, so `LED_RED`/`LED_WHITE` in
`mmhal_set_led()` are no-ops (the yellow LED is repurposed as the dedicated error LED instead).

## Clock tree (confirmed against the real schematic)

`SystemClock_Config()` in `bsp/Core/Src/main.c` uses a 25MHz HXTAL crystal (the schematic's X1
designator is explicitly marked "25MHz"; overridable via the `HXTAL_VALUE` build variable, see
`framework/mk/platform-mm-ekh18-gd32f470.mk`, if you are using a different board) and configures
the main PLL for a 200MHz SYSCLK/AHB, 50MHz APB1, 100MHz APB2. Flash wait states are set to 5, a
commonly-quoted figure for comparable STM32F4/GD32F4 parts at this frequency -- confirm against
the GD32F470 datasheet if you change SYSCLK.

## Flash and RAM

GD32F470VGT6 has 1024KB (1MB) of flash -- bank 0 only, sectors 0-11: four 16K + one 64K + seven
128K sectors, confirmed against the GD32F470xx datasheet (Table 2-1) and the GD32F4xx User Manual
(flash bank 0 is fixed at 1MB for any GD32F4xx part with >=1MB total flash; see the note above about
why this differs from GigaDevice's own default linker script sample) -- and 192KB of main SRAM plus
a separate 64KB TCM region (unused by this port). The datasheet also lists 512KB of total SRAM for
this density, meaning there is likely an additional ~256-320KB "ADDSRAM" region available beyond the
192KB+64KB accounted for here (see the User Manual's memory map, `ADDSRAM` at 0x20030000) that this
port does not yet use -- a possible future enhancement if more RAM headroom is needed.
See `bsp/GD32F470VG_FLASH.ld` for how the 1024KB of flash is partitioned between
bootloader/mmconfig/application.

## Programming

From `examples/porting_assistant/targets/mm-ekh18-gd32f470/`:

- `make` builds `build/porting_assistant.elf` (and `make bin`/`make mbin` for `.bin`/`.mbin`).
- `make reprogram` flashes the ELF over the board's onboard DAPLink probe via OpenOCD, using this
  directory's `openocd.cfg`.
- `make reconfig` provisions the mmconfig store (e.g. the BCF file) over the same OpenOCD
  connection, using `framework/tools/platform/program-configstore.py` and a `config.hjson` you
  provide (default path `../../config.hjson` relative to the target directory). This requires an
  entry for this platform in `framework/tools/platform/configstore.json`, keyed by the OpenOCD
  target name `gd32f470.cpu` (set explicitly via `CHIPNAME` in `openocd.cfg` so it doesn't collide
  with any other STM32F4-family platform that might reuse OpenOCD's generic `stm32f4x` default).

## WLAN GPIO/SPI teardown

Unlike the simpler `mmhal_wlan_assert_reset()`-only pattern used by some other mm-ekh08-*
platforms, `mmhal_wlan_init()`/`mmhal_wlan_deinit()` here fully re-initialise/de-initialise the
MM8108's control GPIOs and SPI0 peripheral on enable/disable (matching `mm-mm8108-ekh05`'s
approach) -- this is to prevent current leakage into a powered-down MM8108 chip. See
`mm_shims/mmhal_wlan.c` for `wlan_hal_gpio_init()`/`wlan_gpio_deinit()`/`wlan_hal_spi_deinit()`.
Since this platform never uses DMA for SPI (see above), the teardown only needs to reset the SPI
peripheral/pins/clock, not any DMA channels.
