/**
 * @file mdi_hw.h
 * @brief 全局外设资源统一定义 (MDI 硬件池头文件)
 *
 * 为应用层提供统一硬件结构定义，避免应用层暴露芯片私有头文件。
 */

#ifndef __MDI_HW_H__
#define __MDI_HW_H__

#include "mdi/legacy/mdi_static.h" /* transitional runtime API */
#include "mdi/legacy/mdi_types.h"
#include "mdi/legacy/mdi.h"

#define MDI_STREAM_WRITE_ASSOCIATIONS \
    mdi_stream_t *: mdi_stream_Write

#define MDI_STREAM_READ_ASSOCIATIONS \
    mdi_stream_t *: mdi_stream_Read

#define MDI_STREAM_IS_BUSY_ASSOCIATIONS \
    mdi_stream_t *: mdi_stream_IsBusy

/*============================================================================
 * 项目硬件资源池定义
 *
 * 结构体内声明项目所有的使用到的外设抽象对象
 *===========================================================================*/

typedef struct {
    /* ---------- GPIO ---------- */
    mdi_gpio_t   *ptLedStatus;      /**< 状态指示 LED */
    mdi_gpio_t   *ptLed[8];          /**< 板级 LED */
    mdi_gpio_t   *ptHeat1;           /**< RTIN1 */
    mdi_gpio_t   *ptHeat2;           /**< RTIN2 */
    mdi_gpio_t   *ptBioPower;        /**< RL0 */
    mdi_gpio_t   *ptBuzzer;          /**< BUZZ */
    mdi_gpio_t   *ptKey[3];          /**< KEY1～KEY3 */
    mdi_gpio_t   *ptEncoder[3];      /**< A、B、按下 */
    mdi_gpio_t   *ptDacWr;           /**< TLC7528 WR# */
    mdi_gpio_t   *ptDacAb;           /**< TLC7528 DAC/AB */
    mdi_gpio_t   *ptDacOe;           /**< TXB0108 OE */

    /* ---------- Stream ---------- */
    mdi_stream_t *ptSerial;        /**< 迪文屏 USART1 */
    mdi_stream_t *ptBoardLink;     /**< 两板 UART4/板级 DATA */

    /* ---------- ADC ---------- */
    mdi_adc_t    *ptNtc[4];         /**< NTC1～NTC4 */
    mdi_adc_t    *ptBioAdc;         /**< BIOADC */
    
} mdi_hardware_t;

/**
 * @brief 全局统一的硬件资源实例
 * 
 * 真正的实例化发生在外设适配层（如 peripheral/AT32/port_mdi.c 中）
 */
extern const mdi_hardware_t HW;

#endif /* __MDI_HW_H__ */
