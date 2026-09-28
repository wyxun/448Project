#ifndef DAC_DRIVER_H
#define DAC_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAC_DRIVER_MAX_FREQUENCY_HZ 20000U
#define DAC_DRIVER_SINE_TABLE_SIZE 256U

typedef enum {
    DAC_DRIVER_RESULT_OK = 0,
    DAC_DRIVER_RESULT_NULL,
    DAC_DRIVER_RESULT_INVALID_ARGUMENT,
    DAC_DRIVER_RESULT_NOT_READY,
    DAC_DRIVER_RESULT_IO,
    DAC_DRIVER_RESULT_FAULT,
} dac_driver_result_t;

typedef enum {
    DAC_DRIVER_STATE_UNINITIALIZED = 0,
    DAC_DRIVER_STATE_IDLE,
    DAC_DRIVER_STATE_RUNNING,
    DAC_DRIVER_STATE_ERROR,
} dac_driver_state_t;

typedef struct {
    /* DAC GPIOs are compile-time resources bound by mdi/instance.h. */
    uint32_t wSampleFrequencyHz;
} dac_driver_cfg_t;

typedef struct {
    uint32_t wPhase;
    uint32_t wPhaseStepPerSample;
    uint8_t chLastValue;
    bool bValueValid;
} dac_driver_wave_t;

typedef struct {
    dac_driver_wave_t tCarrier;
    dac_driver_wave_t tEnvelope;
    uint32_t wSampleFrequencyHz;
    dac_driver_state_t eState;
} dac_driver_t;

dac_driver_result_t dac_driver_Init(
    dac_driver_t *ptThis,
    const dac_driver_cfg_t *ptCfg);

dac_driver_result_t dac_driver_SetCarrierFrequency(
    dac_driver_t *ptThis,
    uint32_t wFrequencyHz);

dac_driver_result_t dac_driver_SetEnvelopeFrequency(
    dac_driver_t *ptThis,
    uint32_t wFrequencyHz);

/* 由板级 TMR1 中断直接调用，消费对象中已换算的执行参数。 */
void dac_driver_IsrStep(dac_driver_t *ptThis);

/* 重新使能 TLC7528 输出并启动已配置的采样定时器。 */
dac_driver_result_t dac_driver_Start(dac_driver_t *ptThis);

/* 停止采样定时器并关闭 TLC7528 输出。 */
dac_driver_result_t dac_driver_Stop(dac_driver_t *ptThis);

#ifdef __cplusplus
}
#endif

#endif /* DAC_DRIVER_H */
