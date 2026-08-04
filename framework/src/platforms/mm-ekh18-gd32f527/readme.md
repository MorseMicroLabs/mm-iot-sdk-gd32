mm-ekh18-gd32f527 Readme {#MM_EKH18_GD32F527_README}
====

__Copyright 2026 Morse Micro__

# Summary

This directory contains the hardware-dependent code needed to use a GD32F527ZMT7 (LQFP144, 4096KB
flash, Cortex-M33) core board with the MM8108 (MMECH18 module). It is derived from
`mm-ekh18-gd32f470`, which was moving off the GD32F470VGT6 (Cortex-M4, 1MB flash, 192KB SRAM) after
hitting resource limits.

`../../bsps/gd32f5xx/Drivers` (CMSIS device support and the `GD32F5xx_standard_peripheral` drivers)
is vendored from GigaDevice's official GD32F5xx firmware library. `bsp/Core/Src/main.c` and the
`mm_shims/*.c` files were written against that real API.

Like the GD32F470 board, this dev board is a bare GPIO breakout ("core board") -- the MMECH18
module must be wired to it with jumper wires rather than plugged in directly.

__READ THIS BEFORE USING ON REAL HARDWARE.__ This platform has been flashed to and run on real
GD32F527ZMT7 + MMECH18 hardware, passing `porting_assistant` **12 of 12** steps (the other 2 of the
14 are preparatory steps that report no verdict): chip ID 0x0809, raw SPI throughput 18,184 kbit/s
at 25MHz, CPU/memory benchmark 265ms confirming a true 200MHz core. One real bug was found and
fixed this way, and it is not reproducible without a physical board:

1. **SRAM ECC + `.noinit` caused an NMI on every cold boot.** See the section below -- this one is
   worth reading in full before porting mm-iot-sdk to any other GD32F5xx part, because it is
   invisible to code review and the symptom points at completely the wrong subsystem.

Two further platform limitations are documented under "Programming" below and are *not* bugs in
this code: OpenOCD cannot program this chip's flash, and any full firmware flash wipes the
mmconfig store.

# SRAM ECC and the `.noinit` section (GD32F5xx-only hazard)

**Symptom.** The board boots, configures its clocks, GPIOs and USART0 correctly, and then goes
completely silent -- no console output at all, not even a banner. It looks exactly like a broken
UART driver. It is not.

**Cause.** GD32F5xx SRAM is ECC-protected, and ECC is enabled by default (`FMC_OBCTL0` bit 28
`ECCEN`, which reads 1 out of the factory). The ECC syndrome bits for a given location are only
established when that location is *written*. Reading SRAM that has never been written since cold
power-on therefore produces an **uncorrectable (two-bit) ECC error**, and on this part that is
reported as an **NMI** -- not a HardFault.

`.noinit` is exactly such a region. The zero-fill loop in `startup_gd32f5xx.S` stops at `_ebss`,
which is precisely where `.noinit` begins, so `.noinit` is never written. `mm_shims/
mmosal_shim_freertos.c` places `preserved_failure_info` there and probes it with:

    if (preserved_failure_info.magic != ASSERT_INFO_MAGIC)

That read raises the NMI *before* the magic check ever gets the chance to reject the garbage. Since
`NMI_Handler` is a weak alias of `Default_Handler` (an infinite loop), the CPU parks there forever
with every peripheral already initialised.

**Diagnostic signature** (any never-written SRAM on a GD32F5xx will reproduce this):

- `xPSR` IPSR field = 2 (NMI), with `CFSR` and `HFSR` both zero -- proving a genuine NMI rather
  than an escalated fault. `SHCSR` bit 5 (`NMIACT`) is set.
- `SYSCFG_STAT` (0x40013824) bit 0 `ECCMEIF0` = 1, "SRAM0 two bits no-correction event flag".
- `SYSCFG_SRAM0ECC` (0x40013828) bits 31:16 `ECCEADDR0` give the faulting **word index** --
  multiply by 4 for the byte address. On the failing board this read 0x61EA, i.e. 0x200187A8,
  which was the exact linked start address of `.noinit`.
- Beware two look-alikes that are *not* the cause: the HXTAL clock monitor also raises an NMI, but
  its flag is `SYSCFG_STAT` bit 14 (`CKMNMIIF`) and it is disabled unless `RCU_CTL` bit 19
  (`CKMEN`) is set. Note also that `SYSCFG_SRAM0ECC` bit 2 is `CKMNMIIE`, an *enable* bit, not an
  error flag.

**Fix.** `ECC_PrimeNoinit()` in `bsp/Core/Src/main.c`, called as the very first statement of
`main()`, zero-fills `.noinit` (delimited by the `_snoinit`/`_enoinit` symbols added to
`bsp/GD32F527ZM_FLASH.ld`). It does this **only on a power-on or brown-out reset**, so `.noinit`
still does its actual job of carrying crash records across warm resets, and it clears the sticky
reset flags afterwards (`RCU_RSTSCK_RSTFC`) -- otherwise every later warm reset would still look
like a cold boot and discard the records.

`.bss`, the FreeRTOS heap (a static array in `.bss`) and the stack all get written before they are
read, so they establish valid ECC on their own and need no priming. This was verified on hardware:
the firmware ran through `SystemClock_Config()`, all peripheral init and the whole of
`xPortStartScheduler()` -- heavy stack use -- without a single ECC event.

**The GD32F470 sibling is unaffected** despite using an equivalent linker script and startup,
because GD32F4xx SRAM has no ECC. This hazard is GD32F5xx-only, and it will bite any future
GD32F5xx platform the moment anything lands in `.noinit`.

## Pinout MMECH18 (confirmed working on hardware)

LQFP144 pin numbers are given for reference when wiring jumpers.

MM8108 pin | GD32F527ZMT7 pin | LQFP144 pin
-----------|------------------|------------
RESET_N    | PC4              | 44
WAKE       | PC5              | 45
BUSY       | PC3              | 29
SPI_IRQ    | PC2              | 28
SPI_CS     | PB12             | 73
SPI_SCK    | PB13             | 74
SPI_MISO   | PB14             | 75
SPI_MOSI   | PB15             | 76

SPI1 is used (PB13/14/15, AF5) with `SPI_CS` as a software-driven GPIO rather than hardware NSS.
`SPI_IRQ` and `BUSY` each get a dedicated single-line EXTI vector (`EXTI2_IRQn`/`EXTI3_IRQn`);
avoid moving them into the shared EXTI10-15 group, which PA10 already uses as a wake source.

`BUSY` is configured `EXTI_TRIG_RISING` only, **not** `EXTI_TRIG_BOTH` -- both-edges fires twice
per toggle and doubles the count in `porting_assistant`'s "Verify BUSY pin" test (proven on the
GD32F470 board). It also has an internal pull-down, so no external one is needed.

Log console (not part of MMECH18): USART0 on PA9 (TX, pin 101) / PA10 (RX, pin 102), 115200 8N1.
These are the board's own CH340X TTL serial pins, not extra wiring.

## Onboard LEDs

Wired VCC -> LED -> resistor -> GPIO (active-LOW):

Colour | Pin | mmhal usage
-------|-----|------------
Red    | PA2 | error LED (`mmhal_set_error_led()`)
Green  | PA3 | `LED_GREEN`

## Clock tree

`SystemClock_Config()` in `bsp/Core/Src/main.c` uses a 25MHz HXTAL and configures the main PLL
(`rcu_pll_config(RCU_PLLSRC_HXTAL, 25, 400, 2, 8)`) for a 200MHz SYSCLK/AHB, 50MHz APB1, 100MHz
APB2. Two GD32F5xx-specific points:

- **PMU high-drive mode is mandatory before 200MHz** and must be enabled *before* switching SYSCLK
  to the PLL: LDO high -> PLL locked -> `pmu_highdriver_mode_enable()` -> wait `PMU_CS_HDRF` ->
  `pmu_highdriver_switch_select()` -> wait `PMU_CS_HDSRF` -> switch to PLLP.
- There is no `fmc_wscnt_set()` call: GD32F5xx handles flash wait states automatically.

GigaDevice's own `system_clock_200m_25m_hxtal()` in `system_gd32f5xx.c` uses `pll_n = 336`, which
actually yields 168MHz, not the 200MHz its name claims. This port uses 400. The 200MHz figure is
confirmed on hardware by `porting_assistant`'s CPU benchmark (265ms, reported as "Good").

The readback check after the clock switch compares against `RCU_SCSS_PLLP`, **not**
`RCU_CKSYSSRC_PLLP` -- these are different bit positions in the same register (status vs control),
and using the wrong one fails the check on every boot even when the switch succeeded. See the
GD32F470 readme for the full history of that bug.

## Flash and RAM

GD32F527ZMT7 has 4096KB of flash in two banks (bank0 0x08000000-0x081FFFFF, bank1
0x08200000-0x083FFFFF; 40 erase pages total: four 16K + one 64K + fifteen 128K per bank).
`bsp/GD32F527ZM_FLASH.ld` only partitions the first 1MB between bootloader/mmconfig/application;
the remaining ~3MB is currently unused and available.

`.vectors` is placed at 0x08000000, the true hardware boot address -- see the GD32F470 readme for
why this matters (linking it elsewhere means a reset silently keeps booting whatever was already
at the boot address, and verify-after-program will still report success).

The linker script maps 256KB of RAM at 0x20000000 plus a separate 64KB TCM region at 0x10000000
(unused by this port). Note the comment at the top of that file also mentions "1MB SRAM", which is
inconsistent with the 256KB `MEMORY` entry beneath it -- confirm against the GD32F527xx datasheet
before relying on more than 256KB.

## SPI clock

SPI1 lives on **APB1**, and the minimum SPI prescaler is `/2` (there is no `/1`). With APB1 at
AHB/4 = 50MHz, **25MHz is the hard maximum for SPI1** and this port already runs there
(`SPI_PSC_2`). Raising APB1 to AHB/2 = 100MHz to go faster would put every APB1 peripheral out of
spec.

Reaching 50MHz would require moving the radio to an APB2 SPI (100MHz / 2). The GD32F470 sibling's
proven configuration is SPI0 on PA4/PA5/PA6/PA7 (AF5) with DMA1 Ch0 RX / Ch5 TX `SUBPERI3`, and
PA4-PA7 are free on this board -- but it needs four wires moved, and the expected gain is only
about 1.55x rather than 2x, because roughly 0.36ms of each 1.32ms transaction is fixed overhead
(DMA setup, semaphore, context switches) rather than bus time.

Bulk transfers (>= `DMA_TRANSFER_MIN_LENGTH`, 16 bytes) use DMA0 Ch3 (RX) / Ch4 (TX), both
`SUBPERI0`; shorter transfers stay polled. Only the RX channel raises a completion interrupt, since
SPI is full duplex and both channels finish together. Note that `DMA_WAIT_TMO` is 10ms and is
enforced with `MMOSAL_ASSERT`, which caps a single DMA transfer at roughly 31KB at 25MHz -- far
above anything currently issued (the largest observed is ~3KB), but the failure mode would be an
assert rather than anything graceful.

## Programming

**OpenOCD cannot program flash on this part.** Its `stm32f2x` driver picks the DBGMCU_IDCODE
address by switching on the CPUID part number (0xC23 = M3, 0xC24 = M4, 0xC27 = M7) and rejects
anything else with "Cannot identify target as a STM32 family" -- the GD32F527 is a Cortex-M33
(part 0xD21). The `stm32l4x`/`stm32l5x`/`stm32u5x` drivers read the correct ARMv8-M address
(0xE0044000, which does hold 0x23090419) but 0x419 is not in their device table, and `stm32h7x`
reads 0x5C001000, which does not exist here. The chip is effectively a chimera: an M33 core with an
F4-style FMC at 0x40023C00 and an F4 device ID at the ARMv8-M DBGMCU address. Fixing this properly
means patching `stm32f2x.c` and rebuilding OpenOCD.

SWD debug, halt, register and memory access via `openocd.cfg` all work normally -- **only flash
programming is affected.** This also means `make reprogram` and `make reconfig` do not work on this
platform, since both go through OpenOCD.

Use `framework/tools/platform/gd32_isp.py` instead. It drives the MCU's on-chip ROM bootloader over
the board's CH340X, so it depends on neither OpenOCD nor the DAPLink probe:

    # Is the MCU alive? (sends 0x7F, expects the 0x79 ACK, dumps the vector table)
    ./tools/platform/gd32_isp.py probe

    # Program and verify
    ./tools/platform/gd32_isp.py flash build/porting_assistant.bin

    # Boot from flash and stream the console
    ./tools/platform/gd32_isp.py console

DAPLink drag-and-drop (`cp image.bin /Volumes/DAPLINK/`) also works but has proven unreliable in
practice -- the volume disappears for long periods, and on one occasion the flash silently reverted
to the previously programmed image.

### The one-key download circuit

The board's CH340X drives the boot pins, and getting the polarity backwards makes a working board
look completely dead:

    DTR -> resistor -> BOOT0     DTR asserted = DTR# low = BOOT0 low = boot from flash
    RTS -> diode    -> NRST      RTS asserted = RTS# low = NRST held low

With the port closed, DTR# idles high, so BOOT0 is high and the board boots the **ISP ROM** rather
than your firmware -- a serial terminal must be open holding DTR asserted for the application to
run at all. macOS asserts both DTR and RTS on open, so a freshly opened port holds the board in
reset until RTS is cleared. Hardware flow control must stay disabled (no `CRTSCTS`), or the tty
layer will drive RTS, which on this board is the reset line.

### Reprogramming the config store

**A full firmware flash wipes mmconfig.** Flash pages 1 and 2 are the CONFIG region (0x08004000,
32KB); an application image starts at 0x08000000 and spans them, and `objcopy` fills that gap with
zeros. After flashing firmware, `porting_assistant`'s "Validate BCF" step (and anything else
reading config) will fail until the store is reprogrammed.

Since `make reconfig` needs OpenOCD, use `program-configstore.py`'s offline mode instead -- `-o`
writes the config store image to a file rather than pushing it to a target:

    MMIOT_ROOT=$PWD python3 tools/platform/program-configstore.py -o cs.bin \
        -p mm-ekh18-gd32f527 write-json ../examples/porting_assistant/config.hjson
    python3 -c "d=open('cs.bin','rb').read(); \
        open('cs32k.bin','wb').write(d + b'\xff'*(32768-len(d)))"
    ./tools/platform/gd32_isp.py flash cs32k.bin --addr 0x08004000

Padding to the full 32KB matters: it makes `gd32_isp.py` erase both pages 1 and 2, which would
otherwise still be full of zeros from the firmware image rather than properly erased. The tool
warns whenever an image it is asked to write overlaps the CONFIG region.
