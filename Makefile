##############################################################################
# Build global options
# NOTE: Can be overridden externally.
#

ifeq ($(USE_OPT),)
  USE_OPT = -O2 -ggdb -fomit-frame-pointer -falign-functions=16
endif

ifeq ($(USE_COPT),)
  USE_COPT =
endif

ifeq ($(USE_CPPOPT),)
  USE_CPPOPT = -fno-rtti
endif

ifeq ($(USE_LINK_GC),)
  USE_LINK_GC = yes
endif

ifeq ($(USE_LDOPT),)
  USE_LDOPT =
endif

ifeq ($(USE_LTO),)
  USE_LTO = no
endif

ifeq ($(USE_VERBOSE_COMPILE),)
  USE_VERBOSE_COMPILE = no
endif

ifeq ($(USE_SMART_BUILD),)
  USE_SMART_BUILD = yes
endif

##############################################################################
# Architecture or project specific options
#

ifeq ($(USE_PROCESS_STACKSIZE),)
  USE_PROCESS_STACKSIZE = 0x400
endif

ifeq ($(USE_EXCEPTIONS_STACKSIZE),)
  USE_EXCEPTIONS_STACKSIZE = 0x400
endif

ifeq ($(USE_FPU),)
  USE_FPU = no
endif

ifeq ($(USE_FPU_OPT),)
  USE_FPU_OPT = -mfloat-abi=$(USE_FPU) -mfpu=fpv5-d16
endif

##############################################################################
# Project, target, sources and paths
#

PROJECT = ch
MCU     = cortex-m7

CHIBIOS  := ../../..
CONFDIR  := ./cfg
BUILDDIR := ./build
DEPDIR   := ./.dep

##############################################################################
# ChibiOS includes
#

include $(CHIBIOS)/os/license/license.mk
include $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk/startup_stm32h7xx.mk
include $(CHIBIOS)/os/hal/hal.mk
include $(CHIBIOS)/os/hal/ports/STM32/STM32H7xx/platform.mk
include $(CHIBIOS)/os/hal/boards/ST_NUCLEO144_H743ZI/board.mk
include $(CHIBIOS)/os/hal/osal/rt-nil/osal.mk
include $(CHIBIOS)/os/rt/rt.mk
include $(CHIBIOS)/os/common/ports/ARMv7-M/compilers/GCC/mk/port.mk
include $(CHIBIOS)/tools/mk/autobuild.mk

include $(CHIBIOS)/os/test/test.mk
include $(CHIBIOS)/test/rt/rt_test.mk
include $(CHIBIOS)/test/oslib/oslib_test.mk

##############################################################################
# Linker script
#

LDSCRIPT = $(STARTUPLD)/STM32H743xI.ld

##############################################################################
# Sources
#

CSRC = $(ALLCSRC) \
       $(TESTSRC) \
       main.c \
       $(wildcard drivers/*.c) \
       $(wildcard ui/*.c) \
       $(wildcard drivers/HallEffect/*.c) \
       $(wildcard drivers/audio/*.c) \
       $(wildcard drivers/midi/*.c) \
       drivers/usb/usb_device.c \
       drivers/usb/usbcfg.c \
       syscalls.c 

##############################################################################
# STM32 HAL – REQUIRED FOR USB DEVICE
#

STM32_HAL_DIR := ./drivers/stm32h7xx-hal-driver

CSRC += \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal_gpio.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal_rcc.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal_pwr.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal_pwr_ex.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal_cortex.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_ll_usb.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal_flash.c \
$(STM32_HAL_DIR)/Src/stm32h7xx_hal_flash_ex.c

##############################################################################
# C / ASM
#

CPPSRC  = $(ALLCPPSRC)
ASMSRC  = $(ALLASMSRC)
ASMXSRC = $(ALLXASMSRC)

##############################################################################
# Includes
#

INCDIR = $(CONFDIR) $(ALLINC) $(TESTINC)

INCDIR += drivers
INCDIR += ui
INCDIR += drivers/HallEffect
INCDIR += drivers/audio
INCDIR += drivers/midi
INCDIR += drivers/usb

INCDIR += $(STM32_HAL_DIR)/Inc

# >>> AJOUT : CMSIS LOCAL AU PROJET
INCDIR += drivers/CMSIS/Include
INCDIR += drivers/CMSIS/Device/ST/STM32H7xx/Include

##############################################################################
# Warnings
#

CWARN   = -Wall -Wextra -Wundef -Wstrict-prototypes
CPPWARN = -Wall -Wextra -Wundef

##############################################################################
# User defines
#

UDEFS = -DSTM32_ENFORCE_H7_REV_XY \
        -DUSE_HAL_DRIVER \
        -DSTM32H743xx

UADEFS =

UINCDIR =
ULIBDIR =
ULIBS   =

##############################################################################
# Rules
#

RULESPATH = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/mk
include $(RULESPATH)/arm-none-eabi.mk
include $(RULESPATH)/rules.mk

##############################################################################
# Custom rules
#
##############################################################################
