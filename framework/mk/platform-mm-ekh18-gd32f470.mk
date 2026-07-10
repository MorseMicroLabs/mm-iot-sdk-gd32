#
# Copyright 2026 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#
# Platform makefile for mm-ekh18-gd32f470.
#
# The CMSIS device support and GD32F4xx_standard_peripheral driver files under
# framework/src/bsps/gd32f4xx/Drivers are vendored unmodified from GigaDevice's official
# GD32F4xx_Firmware_Library V3.3.3, the same way stm32cubeh7/stm32cubewb/stm32cubeu5 are vendored
# for the STM32 platforms.
#
# This target is built for a GD32F470VGT6 (LQFP100, 1024KB/1MB flash) core board, confirmed against the
# schematic packaged with that board ("SCH_LQFP100-14_14-V03_ARM_25M"). The wiring uses free/unused
# pins on that board's H2 GPIO breakout header (see
# framework/src/platforms/mm-ekh18-gd32f470/readme.md for the pin table). If you are targeting a
# different GD32F470 dev board, re-check the pin mapping in bsp/Core/Inc/main.h and the flash
# layout in bsp/GD32F470VG_FLASH.ld against your own schematic/part before use.
#
# NOTE: this platform targets an MM8108 (MMECH18 module), not MM6108/MMECH08. This platform was
# originally developed under the name "mm-ekh08-gd32f470" (built against the wrong chip) and was
# renamed to "mm-ekh18-gd32f470" once the chip was corrected. The old mm-ekh08-gd32f470 platform/
# Makefile/target still exist as thin forwarding stubs to this file (files in this workspace cannot
# be deleted, only content-overwritten). MMECH08 (MM6108) and MMECH18 (MM8108) modules share the
# same pin-compatible control/SPI interface (confirmed against
# framework/src/platforms/mm-ekh18-wb55/readme.md's pinout table, which is identical to
# mm-ekh08-wb55's), so no pin/wiring changes were needed -- only the chip-selection variables below.
#
# IMPORTANT: MCU deep sleep is not implemented (see mm_shims/mmhal_os.c). DMA-based SPI transfers
# ARE implemented (see mm_shims/mmhal_wlan.c and bsp/Core/Inc/main.h for the SPI0 DMA1
# channel/subperipheral mapping and its provenance) for transfers >= DMA_TRANSFER_MIN_LENGTH bytes;
# smaller transfers stay polled. mmhal_wlan_deinit() fully tears down the SPI peripheral, its DMA
# channels, and MM8108 control GPIOs on disable (matching mm-mm8108-ekh05) to prevent current
# leakage.

CORE = arm-cortex-m4f
PLATFORM_PATH = src/platforms/mm-ekh18-gd32f470

MMHAL_CHIP_TYPE ?= mmhal_mm8108
BUILD_DEFINES += MMHAL_CHIP_TYPE=$(MMHAL_CHIP_TYPE)

FW_MBIN ?= mm8108b2-rl.mbin

# Platform specific files
BSP_DIR = $(PLATFORM_PATH)/bsp

BSP_SRCS_MAIN_C ?= Core/Src/main.c
BSP_SRCS_C += $(BSP_SRCS_MAIN_C)

BSP_SRCS_H += Core/Inc/main.h
BSP_SRCS_H += Core/Inc/gd32f4xx_libopt.h

BSP_LD_PREFIX = GD32F470VG_FLASH

# Driver files vendored unmodified from GigaDevice's GD32F4xx_Firmware_Library V3.3.3
BSP_DRIVERS_DIR = src/bsps/gd32f4xx

BSP_DRIVERS_SRCS_S += Drivers/CMSIS/GD/GD32F4xx/Source/startup_gd32f450_470.S

BSP_DRIVERS_SRCS_C += Drivers/CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_rcu.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_gpio.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_spi.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_usart.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_exti.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_syscfg.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_fmc.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_misc.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_trng.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F4xx_standard_peripheral/Source/gd32f4xx_dma.c

BSP_DRIVERS_SRCS_H += Drivers/CMSIS/GD/GD32F4xx/Include/gd32f4xx.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/GD/GD32F4xx/Include/system_gd32f4xx.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/core_cm4.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/core_cm4_simd.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/core_cmFunc.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/core_cmInstr.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_rcu.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_gpio.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_spi.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_usart.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_exti.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_syscfg.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_fmc.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_misc.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_trng.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F4xx_standard_peripheral/Include/gd32f4xx_dma.h

MM_SHIM_DIR     = $(PLATFORM_PATH)/mm_shims
MM_SHIM_SRCS_C += $(patsubst $(MMIOT_ROOT)/$(MM_SHIM_DIR)/%,%,$(wildcard $(MMIOT_ROOT)/$(MM_SHIM_DIR)/*.c))
MM_SHIM_SRCS_H += mmport.h
MM_SHIM_SRCS_C := $(filter-out mmosal_shim_freertos.c mmosal_shim_alloc.c mmosal_shim_libc_stubs.c,$(MM_SHIM_SRCS_C))

MM_SHIM_OS_SRCS_C ?= mmosal_shim_freertos.c mmosal_shim_alloc.c mmosal_shim_libc_stubs.c
MM_SHIM_SRCS_C += $(MM_SHIM_OS_SRCS_C)
MM_SHIM_SRCS_H += endian.h

MMIOT_SRCS_C += $(addprefix $(BSP_DIR)/,$(BSP_SRCS_C))
MMIOT_SRCS_C += $(addprefix $(BSP_DRIVERS_DIR)/,$(BSP_DRIVERS_SRCS_C))
MMIOT_SRCS_C += $(addprefix $(MM_SHIM_DIR)/,$(MM_SHIM_SRCS_C))
MMIOT_SRCS_S += $(addprefix $(BSP_DRIVERS_DIR)/,$(BSP_DRIVERS_SRCS_S))
MMIOT_SRCS_H += $(addprefix $(BSP_DIR)/,$(BSP_SRCS_H))
MMIOT_SRCS_H += $(addprefix $(BSP_DRIVERS_DIR)/,$(BSP_DRIVERS_SRCS_H))
MMIOT_SRCS_H += $(addprefix $(MM_SHIM_DIR)/,$(MM_SHIM_SRCS_H))

MMIOT_INCLUDES += $(BSP_DIR)
MMIOT_INCLUDES += $(BSP_DIR)/Core/Inc
MMIOT_INCLUDES += $(BSP_DRIVERS_DIR)/Drivers/CMSIS/Include
MMIOT_INCLUDES += $(BSP_DRIVERS_DIR)/Drivers/CMSIS/GD/GD32F4xx/Include
MMIOT_INCLUDES += $(BSP_DRIVERS_DIR)/Drivers/GD32F4xx_standard_peripheral/Include
MMIOT_INCLUDES += $(MM_SHIM_DIR)

BUILD_DEFINES += GD32F470

# Value of the HXTAL crystal fitted to the board, in Hz. 25MHz is confirmed correct for the real
# GD32F470VGT6 dev board this platform targets (X1 on the schematic is explicitly marked "25MHz");
# override with e.g. `make HXTAL_VALUE=8000000` if you are using a different board.
HXTAL_VALUE ?= 25000000
BUILD_DEFINES += HXTAL_VALUE=$(HXTAL_VALUE)

ifneq ($(ENABLE_EXT_XTAL_INIT),)
BUILD_DEFINES += ENABLE_EXT_XTAL_INIT=$(ENABLE_EXT_XTAL_INIT)
endif

CONLYFLAGS += -include $(MMIOT_ROOT)/$(BSP_DRIVERS_DIR)/Drivers/CMSIS/GD/GD32F4xx/Include/gd32f4xx.h
CFLAGS-$(BSP_DRIVERS_DIR) += -Wno-c++-compat
CFLAGS-$(BSP_DRIVERS_DIR) += -Wno-unused-parameter
CFLAGS-$(MM_SHIM_DIR) += -Wno-unused-parameter

LINKFLAGS += --entry Reset_Handler
LINKFLAGS += -Wl,--defsym,HEAPSIZE=0x1000
LINKFLAGS += -Wl,--defsym,STACKSIZE=0x1000
LINKFLAGS += -Wl,--defsym,_CSTACK_Base=0x00000000
LINKFLAGS += -Wl,--defsym,_CSTACK_Length=0x1000
LINKFLAGS += -Wl,--undefined=uxTopUsedPriority
LINKFLAGS += -Wl,--print-memory-usage

BSP_LD_POSTFIX ?= .ld
BSP_LD_FILES += $(BSP_DIR)/$(BSP_LD_PREFIX)$(BSP_LD_POSTFIX)
