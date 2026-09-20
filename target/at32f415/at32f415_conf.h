/**
 * @file at32f415_conf.h
 * @brief AT32F415 peripheral library configuration.
 * @author Codex
 * @date 2026-09-18
 */
#ifndef __AT32F415_CONF_H
#define __AT32F415_CONF_H

#define HEXT_VALUE               ((uint32_t)8000000)
#define HEXT_STARTUP_TIMEOUT     ((uint16_t)0x3000)
#define HICK_VALUE               ((uint32_t)8000000)
#define LEXT_VALUE               ((uint32_t)32768)

#define CRM_MODULE_ENABLED
#define GPIO_MODULE_ENABLED
#define MISC_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define DEBUG_MODULE_ENABLED
#define TMR_MODULE_ENABLED
#define USART_MODULE_ENABLED
#define DMA_MODULE_ENABLED
#define ADC_MODULE_ENABLED

#include "at32f415_crm.h"
#include "at32f415_gpio.h"
#include "at32f415_misc.h"
#include "at32f415_flash.h"
#include "at32f415_debug.h"
#include "at32f415_tmr.h"
#include "at32f415_usart.h"
#include "at32f415_dma.h"
#include "at32f415_adc.h"

#endif
