# =============================================================================
# target/at32f415/target.mk
# AT32F415RBT7 — Cortex-M4, 128 KiB flash / 32 KiB RAM
# =============================================================================

CPU_FLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
C_DEFS += -DAT32F415RBT7 -DUSE_STDPERIPH_DRIVER

CHIPLIB_ROOT = vendor/cortex-m/AT32F415_Firmware_Library/libraries
CMSIS_CORE   = vendor/cortex-m/cmsis_core
CMSIS_DEV    = $(CHIPLIB_ROOT)/cmsis/cm4/device_support
DRV_INC      = $(CHIPLIB_ROOT)/drivers/inc
DRV_SRC      = $(CHIPLIB_ROOT)/drivers/src

LDSCRIPT     = target/at32f415/AT32F415xB_FLASH.ld
STARTUP_S    = $(CMSIS_DEV)/startup/gcc/startup_at32f415.s
SYSTEM_C     = $(CMSIS_DEV)/system_at32f415.c
IT_C         = target/at32f415/at32f415_it.c
TARGET_INCLUDES = -I$(DRV_INC) -Itarget/at32f415

USE_DRV_CRM    ?= 1
USE_DRV_GPIO   ?= 1
USE_DRV_MISC   ?= 1
USE_DRV_FLASH  ?= 1
USE_DRV_TMR    ?= 1
USE_DRV_USART  ?= 1
USE_DRV_DMA    ?= 1
USE_DRV_ADC    ?= 1
USE_DRV_DEBUG  ?= 1

DRV_SOURCES =
ifeq ($(USE_DRV_CRM),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_crm.c
endif
ifeq ($(USE_DRV_GPIO),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_gpio.c
endif
ifeq ($(USE_DRV_MISC),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_misc.c
endif
ifeq ($(USE_DRV_FLASH),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_flash.c
endif
ifeq ($(USE_DRV_TMR),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_tmr.c
endif
ifeq ($(USE_DRV_USART),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_usart.c
endif
ifeq ($(USE_DRV_DMA),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_dma.c
endif
ifeq ($(USE_DRV_ADC),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_adc.c
endif
ifeq ($(USE_DRV_DEBUG),1)
    DRV_SOURCES += $(DRV_SRC)/at32f415_debug.c
endif

CHIP_SOURCES = $(DRV_SOURCES)
MODUS_USE_DEFAULT_PERFC_PORT = 1
