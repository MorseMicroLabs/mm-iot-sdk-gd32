# Provenance of the files in this directory {#GD32F4XX_BSP_PROVENANCE}

The files under this directory (`Drivers/CMSIS/GD/GD32F4xx` and
`Drivers/GD32F4xx_standard_peripheral`) are vendored **verbatim, unmodified** from GigaDevice's
official `GD32F4xx_Firmware_Library_V3.3.3`, in the same way `stm32cubeh7`/`stm32cubewb`/
`stm32cubeu5` are vendored (unmodified) for the STM32 platforms elsewhere in this SDK.

This directory started life as a hand-written, best-effort reconstruction of the GD32F4xx
CMSIS/driver API (written from general knowledge of the register- and pin-compatible STM32F4
family, because the real vendor library was not available at the time). Once the real
`GD32F4xx_Firmware_Library_V3.3.3` archive was obtained, every file in this directory was replaced
with the genuine vendor source, and `framework/src/platforms/mm-ekh18-gd32f470/bsp/Core/Src/main.c`
and the `mm_shims/*.c` files were updated to match the real API (a small number of function
signatures -- e.g. `rcu_pll_config()` taking a 5th `pll_q` argument, bus-specific
`RCU_APB1_CKAHB_DIV4`/`RCU_APB2_CKAHB_DIV2` naming, `fmc_wscnt_set(WS_WSCNT_5)` instead of a raw
register write -- differed from the original hand-written guesses).

Only the subset of the vendor library actually needed by `mm-ekh18-gd32f470` is compiled (see
`BSP_DRIVERS_SRCS_C`/`BSP_DRIVERS_SRCS_H` in `framework/mk/platform-mm-ekh18-gd32f470.mk`), but the
files themselves are unmodified vendor source, not a rewritten subset.

**Naming note:** this platform was originally developed (and is still occasionally referenced in
older comments) under the name `mm-ekh08-gd32f470`; it has since been renamed to
`mm-ekh18-gd32f470` to reflect the corrected MM8108/MMECH18 chip target (see "Chip selection"
below). The old `mm-ekh08-gd32f470` directory still exists but is superseded/forwarding.

The plain ARM CMSIS core files actually used (`core_cm4.h`, `core_cm4_simd.h`, `core_cmFunc.h`,
`core_cmInstr.h` under `Drivers/CMSIS/Include`) are also the ones bundled by GigaDevice with this
firmware library version (V3.30 CMSIS), rather than the newer CMSIS5 split used by some other
vendors' packages. A handful of leftover CMSIS5-style files (`cmsis_compiler.h`, `cmsis_gcc.h`,
`cmsis_version.h`, `mpu_armv7.h`) remain in `Drivers/CMSIS/Include` from the original hand-written
version of this port; they are harmless and unreferenced by the build (left in place only because
files in this workspace cannot be deleted, only overwritten).

## Board specifics

This platform targets a real GD32F470VGT6 (LQFP100, 1024KB/1MB flash) core board. The pin mapping,
crystal value, and flash/RAM sizes are all confirmed against the schematic and other vendor
documentation packaged with that specific board (see
`framework/src/platforms/mm-ekh18-gd32f470/readme.md` for the details and sources) -- they are not
placeholders. Concretely:

1. **The MMECH18-to-GD32F470VGT6 pin mapping** in
   `framework/src/platforms/mm-ekh18-gd32f470/bsp/Core/Inc/main.h` is taken from the real
   schematic's H2 GPIO breakout header.
2. **The HXTAL crystal value** (`HXTAL_VALUE` in `platform-mm-ekh18-gd32f470.mk`, 25MHz) matches
   the schematic's X1 crystal designator.
3. **The flash size (1024KB) and partitioning** in `bsp/GD32F470VG_FLASH.ld` matches the
   GD32F470xx datasheet (Table 2-1) and GD32F4xx User Manual (flash bank 0 is fixed at 1MB for any
   part with >=1MB total flash) -- **not** GigaDevice's own default GCC linker script sample for
   this density (`gd32f470xG_flash.ld`, which states 512KB and appears to be a copy-paste leftover
   from the smaller "E" density template; its `xE`/`xG` scripts are byte-identical apart from the
   filename). This was confirmed in practice when a real build needed ~520KB of flash for
   `porting_assistant`, which the datasheet/user-manual figure explains and the vendor sample script
   does not. Earlier iterations of this port initially followed the vendor sample (512KB) before
   this was caught; also not the 3MB "K"-suffix part this port originally assumed before the real
   board was available.
4. **The flash sector map used for the mmconfig partition**
   (`fmc_bank0_sector_of_address()` in `mm_shims/mmhal_flash.c`) is confirmed directly from the
   real `gd32f4xx_fmc.h` (`CTL_SECTOR_NUMBER_0`..`_11`) for GD32F470VG's single 1MB flash bank.
5. **The mmconfig CONFIG region is 32K (two 16K sectors), not the 16K single-sector convention**
   most other mm-ekh08-*/mm-mm6108/8108-ekh05 platforms use. Those platforms' physical flash
   sectors are smaller than 16K, so a 16K CONFIG region still splits cleanly into two
   independently-erasable sectors for mmconfig's A/B redundant storage; GD32F470's smallest sector
   is 16K itself, so a single-sector 16K CONFIG region could not be split into two
   independently-erasable halves at all (`mmconfig_erase_partition()` in
   `framework/src/mmconfig/mmconfig.c` would erase a full 16K sector while trying to erase an 8K
   partition, corrupting the other partition in the process). This was caught while wiring up
   `make reconfig` (config-store provisioning via OpenOCD) for this platform -- see
   `bsp/GD32F470VG_FLASH.ld` and `mm_shims/mmhal_flash.c`. `framework/tools/platform/
   configstore.json` was also updated with a `gd32f470.cpu` entry (the OpenOCD target name, set
   explicitly via `CHIPNAME` in `openocd.cfg`) so `program-configstore.py` can auto-detect this
   platform's mmconfig geometry.

## Chip selection: MM8108, not MM6108

This platform pairs the GD32F470VGT6 with an MM8108-based MMECH18 module, not MM6108/MMECH08 as
originally assumed. The fix was a `MMHAL_CHIP_TYPE`/`FW_MBIN` swap (`mmhal_mm8108`/
`mm8108b2-rl.mbin`, confirmed against `framework/mk/platform-mm-mm8108-ekh05.mk`) plus updating
MM6108/MMECH08 references in comments and this platform's readme.md -- no pin/wiring changes were
needed, since MMECH08 and MMECH18 modules share an identical control/SPI pin interface (compare
`mm-ekh08-wb55/readme.md` and `mm-ekh18-wb55/readme.md`'s pinout tables). The directory was
subsequently renamed from `mm-ekh08-gd32f470` to `mm-ekh18-gd32f470` (see the "Naming note" above)
to match the corrected chip, and now also includes the fuller MM8108 GPIO/SPI teardown pattern
used by `mm-mm8108-ekh05` (see `mm_shims/mmhal_wlan.c`'s `mmhal_wlan_init()`/
`mmhal_wlan_deinit()`).

## Build status

This platform has been confirmed to build and link successfully with a real `arm-none-eabi-gcc`
toolchain against the `porting_assistant` example, and has been flashed to real GD32F470VGT6
hardware via `make reprogram`/OpenOCD.

## Vector table placement: BOOTLOADER, not FLASH

The first real hardware flash attempt did not boot -- the board kept running its original factory
demo firmware after `make reprogram` reported success. Root cause: `.vectors` was linked into the
FLASH region (0x0800C000) rather than BOOTLOADER (0x08000000). Cortex-M4 unconditionally fetches
its initial SP/PC from 0x08000000 in hardware at cold reset (boot-from-main-flash mode, confirmed
active since the factory demo runs at all) -- this is not software-configurable. Since nothing in
the ELF targeted 0x08000000, `program ... verify reset exit` never touched that address, so the
original factory firmware's vector table there was left completely intact and kept booting.
Sibling mm-ekh08-*/mm-mm8108-ekh05 platforms all place `.vectors` at their FLASH region's origin
(past their own BOOTLOADER/CONFIG split) -- this assumes a real, separately-provisioned bootloader
already occupies BOOTLOADER and jumps into the app, but no bootloader source/build target exists
anywhere in this SDK for any platform, and this bare third-party dev board doesn't ship with one
either. Separately, GigaDevice's vendored `system_gd32f4xx.c`:`SystemInit()` unconditionally calls
`nvic_vector_table_set(NVIC_VECTTAB_FLASH, VECT_TAB_OFFSET)` (0x08000000 + 0), forcibly resetting
VTOR there every boot -- so even a hypothetical successful jump from a bootloader would have had
its interrupt vector table hijacked unless `.vectors` genuinely lives at 0x08000000. Fixed by
placing `.vectors` (and the still-empty `.bootloader` placeholder) in BOOTLOADER instead of FLASH
-- see the header comment in `bsp/GD32F470VG_FLASH.ld` for the full writeup. `.text`/`.rodata`/etc.
are unaffected and still live in FLASH; the vector table only needs to *contain the address* of
Reset_Handler, not be adjacent to it.

## Clock-switch status check compared against the wrong constant

After fixing the vector table, the board still didn't run the application -- it was hitting
`Error_Handler()` on every boot, immediately after clock setup, silently (no UART output reaches
that point, since `MX_USART0_Init()` runs later in `main()`). Live GDB register inspection (not
just code review) showed HXTAL stabilized fine (`rcu_osci_stab_wait(RCU_HXTAL)` returned SUCCESS),
`rcu_pll_config()` accepted its parameters and returned SUCCESS, and the PLL locked fine
(`rcu_osci_stab_wait(RCU_PLL_CK)` also SUCCESS) -- but `rcu_system_clock_source_get()` returned `8`
where the code expected `2` (`RCU_CKSYSSRC_PLLP`), so `SystemClock_Config()` concluded the switch
to PLL had failed and called `Error_Handler()`, even though the clock tree was actually correctly
configured and running at 200MHz. Root cause: `RCU_CKSYSSRC_PLLP` is `CFG0_SCS(2)` (bits[1:0] of
CFG0, the "request this source" field, value `2`) -- it is only meaningful as the *input* to
`rcu_system_clock_source_config()`. The *readback* from `rcu_system_clock_source_get()` reflects
CFG0.SCSS (bits[3:2]), a different bit position in the same register, and must be compared against
`RCU_SCSS_PLLP` (`CFG0_SCSS(2)`, value `8`) instead. `bsp/Core/Src/main.c` used the wrong constant
for that comparison; fixed to use `RCU_SCSS_PLLP`.

## BUSY EXTI trigger: BOTH edges instead of rising-only

With clock/vector/wiring issues resolved and the MMECH18 module attached, `porting_assistant`'s
"Verify BUSY pin" test failed with `BUSY pin IRQ hander was not called as expected. Expected 2
invocations, but was invoked 4 times`. The test toggles the MM8108's own GPIO2 (hardware-routed to
its physical BUSY pin) HIGH/LOW twice and expects exactly one IRQ per assertion (rising edge only).
Both `bsp/Core/Src/main.c`:`MX_GPIO_Init()` and `mm_shims/mmhal_wlan.c`:`wlan_hal_gpio_init()`
configured `BUSY_IRQ_LINE` with `EXTI_TRIG_BOTH` (fires on both rising and falling edges) instead of
`EXTI_TRIG_RISING`, so each of the 2 toggle cycles fired twice (4 total) instead of once (2 total).
Sibling platform `mm-mm8108-ekh05` uses `LL_EXTI_TRIGGER_RISING` for this exact line, confirming
rising-only is correct. Fixed in both places.

## mmhal_flash.c: exact-match sector lookup instead of a range check

With SPI/BUSY wiring fixed, `porting_assistant`'s "Validate BCF" test step crashed with
`MMOSAL_ASSERT` failure `... line 932 in <hash>`, which decodes to the second assertion inside
`mmconfig_init()` (`framework/src/mmconfig/mmconfig.c`):

```c
MMOSAL_ASSERT(mmhal_flash_getblocksize(mmconfig_partition->partition_start) != 0);
MMOSAL_ASSERT(
    mmhal_flash_getblocksize(
        mmconfig_partition->partition_start + mmconfig_partition->partition_size - 1) != 0);
```

The second call probes the *last byte* of the CONFIG partition (`0x08004000 + 0x8000 - 1 =
0x0800BFFF`), which is the last byte of sector 2 (`0x08008000`-`0x0800BFFF`), not sector 2's start
address. `mm_shims/mmhal_flash.c`'s `fmc_bank0_sector_of_address()` matched addresses via exact
equality against each sector's *start* address only (`address == sector_starts[ii]`), so
`0x0800BFFF` matched nothing, returned `UINT32_MAX`, and `mmhal_flash_getblocksize()` returned `0`,
tripping the assert. This was a latent bug in the original single-sector CONFIG design too
(`0x08004000 + 0x4000 - 1 = 0x08007FFF`, also not a sector start) -- it just took reaching the BCF
read path on real hardware to actually exercise it. Fixed by changing the lookup to a proper
`[sector.start, sector.start + sector.size)` range-containment check over the full bank-0 sector
map (tracking each sector's size alongside its start address, rather than assuming a fixed 16K).

## What is still not verified

1. **MCU deep sleep** is not implemented (`mmhal_sleep_prepare()` only ever returns
   `MMHAL_SLEEP_SHALLOW`/`MMHAL_SLEEP_DISABLED`) -- see `mm_shims/mmhal_os.c`.
2. **DMA-based SPI transfers** are not implemented; all SPI transfers are polled -- see
   `mm_shims/mmhal_wlan.c`. (The GPIO/SPI teardown added in `mmhal_wlan_deinit()` only resets the
   SPI peripheral, pins, and clock accordingly -- there are no DMA channels to tear down.)
3. **WLAN bring-up beyond BCF validation** (full connect/data-path testing) is still unconfirmed on
   physical silicon -- SPI/GPIO wiring, clock stability, and BUSY IRQ handling have now been
   verified end-to-end via `porting_assistant` on real hardware, up to and including BCF loading.

If you are using a *different* GD32F470 board than the one this platform targets, re-verify the pin
mapping, crystal value, and flash layout against your own schematic before flashing real hardware.
