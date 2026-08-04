#
# Copyright 2026 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#
# Platform makefile for mm-ekh18-gd32f527.
#
# The CMSIS device support and GD32F5xx_standard_peripheral driver files under
# framework/src/bsps/gd32f5xx/Drivers are vendored unmodified from GigaDevice's official
# GD32F5xx firmware library V1.2.0, the same way stm32cubeh7/stm32cubewb/stm32cubeu5 are vendored
# for the STM32 platforms.
#
# This target is built for a GD32F527ZMT7 (LQFP144, 4096KB/4MB flash, 1MB SRAM) core board
# (the "GD32F527ZMT7 V1.1小系统板").  The wiring reuses the same SPI/EXTI/GPIO pins as
# mm-ekh18-gd32f470 where possible (see framework/src/platforms/mm-ekh18-gd32f527/bsp/Core/Inc/
# main.h for the full pin table), with the following differences from the F470 port:
#   - Cortex-M33 core (arm-cortex-m33f) instead of Cortex-M4
#   - SPI1 (PB13/14/15) with DMA0 Ch3/4 SUBPERI0, instead of SPI0 with DMA1
#   - EXTI2/PC2 (SPI_IRQ) and EXTI3/PC3 (BUSY) instead of EXTI1/EXTI0
#   - 256KB SRAM (vs 192KB), 4MB flash (same first-1MB sector layout)
#   - PMU high-drive mode required before 200MHz (handled in SystemClock_Config())
#   - No fmc_wscnt_set() -- GD32F527 handles flash wait states automatically
#
# IMPORTANT: MCU deep sleep is not implemented (see mm_shims/mmhal_os.c). DMA-based SPI transfers
# ARE implemented for transfers >= DMA_TRANSFER_MIN_LENGTH bytes; smaller transfers stay polled.

CORE = arm-cortex-m33f
PLATFORM_PATH = src/platforms/mm-ekh18-gd32f527

MMHAL_CHIP_TYPE ?= mmhal_mm8108
BUILD_DEFINES += MMHAL_CHIP_TYPE=$(MMHAL_CHIP_TYPE)

FW_MBIN ?= mm8108b2-rl.mbin

# Platform specific files
BSP_DIR = $(PLATFORM_PATH)/bsp

BSP_SRCS_MAIN_C ?= Core/Src/main.c
BSP_SRCS_C += $(BSP_SRCS_MAIN_C)

ifneq ($(BUILD_WITH_MBEDTLS_ECC_HW_CRYPTO),)
# Enable GD32F527 PKCAU hardware acceleration for ECC (mbedTLS ECP alternative implementation)
BUILD_DEFINES += MBEDTLS_ECP_ALT=1

BSP_SRCS_C += Core/Src/ecp_alt.c
BSP_SRCS_C += Core/Src/ecp_hal.c
BSP_SRCS_C += Core/Src/ecp_curves_alt.c
BSP_SRCS_H += Core/Inc/ecp_alt.h
endif

BSP_SRCS_H += Core/Inc/main.h
BSP_SRCS_H += Core/Inc/gd32f5xx_libopt.h

BSP_LD_PREFIX = GD32F527ZM_FLASH

# Driver files vendored from GigaDevice's GD32F5xx firmware library V1.2.0
BSP_DRIVERS_DIR = src/bsps/gd32f5xx

BSP_DRIVERS_SRCS_S += Drivers/CMSIS/GD/GD32F5xx/Source/GCC/startup_gd32f5xx.S

BSP_DRIVERS_SRCS_C += Drivers/CMSIS/GD/GD32F5xx/Source/system_gd32f5xx.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_rcu.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_gpio.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_spi.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_usart.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_exti.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_syscfg.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_fmc.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_misc.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_trng.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_pkcau.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_dma.c
BSP_DRIVERS_SRCS_C += Drivers/GD32F5xx_standard_peripheral/Source/gd32f5xx_pmu.c

BSP_DRIVERS_SRCS_H += Drivers/CMSIS/GD/GD32F5xx/Include/gd32f5xx.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/GD/GD32F5xx/Include/system_gd32f5xx.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/core_cm33.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/cmsis_compiler.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/cmsis_gcc.h
BSP_DRIVERS_SRCS_H += Drivers/CMSIS/Include/cmsis_version.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_rcu.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_gpio.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_spi.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_usart.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_exti.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_syscfg.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_fmc.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_misc.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_trng.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_pkcau.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_dma.h
BSP_DRIVERS_SRCS_H += Drivers/GD32F5xx_standard_peripheral/Include/gd32f5xx_pmu.h

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
MMIOT_INCLUDES += $(BSP_DRIVERS_DIR)/Drivers/CMSIS/GD/GD32F5xx/Include
MMIOT_INCLUDES += $(BSP_DRIVERS_DIR)/Drivers/GD32F5xx_standard_peripheral/Include
MMIOT_INCLUDES += $(MM_SHIM_DIR)

BUILD_DEFINES += GD32F527

# Value of the HXTAL crystal fitted to the board, in Hz. 25MHz is confirmed correct for the
# GD32F527ZMT7 V1.1小系统板 (X1 on the schematic is marked "25MHz"); override with e.g.
# `make HXTAL_VALUE=8000000` if you are using a different board.
HXTAL_VALUE ?= 25000000
BUILD_DEFINES += HXTAL_VALUE=$(HXTAL_VALUE)

ifneq ($(ENABLE_EXT_XTAL_INIT),)
BUILD_DEFINES += ENABLE_EXT_XTAL_INIT=$(ENABLE_EXT_XTAL_INIT)
endif

CONLYFLAGS += -include $(MMIOT_ROOT)/$(BSP_DRIVERS_DIR)/Drivers/CMSIS/GD/GD32F5xx/Include/gd32f5xx.h
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
