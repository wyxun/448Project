#ifndef DAC_DRIVER_H
#define DAC_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAC_DRIVER_MAX_FREQUENCY_HZ 20000U
#define DAC_DRIVER_SINE_TABLE_SIZE 256U
#define DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT 100U

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

typedef enum {
    DAC_DRIVER_WAVE_SINE = 0,
    DAC_DRIVER_WAVE_DIGITAL,
} dac_driver_wave_type_t;

typedef struct {
    /* DAC GPIOs are compile-time resources bound by mdi/instance.h. */
    uint32_t wSampleFrequencyHz;
} dac_driver_cfg_t;

typedef struct {
    uint32_t wPhase;
    uint32_t wPhaseStepPerSample;
    uint16_t hwLastValue;
    uint8_t chLastValue;
    bool bValueValid;
} dac_driver_wave_t;

typedef struct {
    dac_driver_wave_t tCarrier;
    dac_driver_wave_t tEnvelope;
    uint32_t wSampleFrequencyHz;
    dac_driver_state_t eState;
    dac_driver_wave_type_t eEnvelopeWaveType;
    uint8_t aachCarrierTable[2][DAC_DRIVER_SINE_TABLE_SIZE];
    volatile uint8_t chActiveCarrierTable;
    const uint8_t *pchActiveCarrierTable;
    const uint8_t *pchActiveEnvelopeTable;
} dac_driver_t;

dac_driver_result_t dac_driver_Init(
    dac_driver_t *ptThis,
    const dac_driver_cfg_t *ptCfg);

/**
 * @brief Configure the envelope waveform type (sine or digital square).
 * @param ptThis Caller-owned module instance.
 * @param eWaveType Envelope wave mode.
 * @return Module result code; invalid inputs leave the mode unchanged.
 */
dac_driver_result_t dac_driver_SetWaveType(
    dac_driver_t *ptThis,
    dac_driver_wave_type_t eWaveType);

/**
 * @brief Validate and set carrier and envelope frequencies as one update.
 * @param ptThis Caller-owned module instance.
 * @param wCarrierFrequencyHz DACB/carrier frequency in Hz.
 * @param wEnvelopeFrequencyHz DACA/envelope frequency in Hz.
 * @return Module result code; invalid inputs do not update either channel.
 */
dac_driver_result_t dac_driver_SetFrequencies(
    dac_driver_t *ptThis,
    uint32_t wCarrierFrequencyHz,
    uint32_t wEnvelopeFrequencyHz);

/**
 * @brief Set DACB amplitude by building and swapping its sample table.
 * @param ptThis Caller-owned module instance.
 * @param chAmplitudePercent Peak amplitude in 0..100 percent (valley at 0).
 * @return Result code; invalid inputs leave the active table unchanged.
 */
dac_driver_result_t dac_driver_SetCarrierAmplitude(
    dac_driver_t *ptThis,
    uint8_t chAmplitudePercent);

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
