/**
 * @file dac_driver.c
 * @brief TLC7528 dual-channel sine-wave DAC driver.
 */

#include "dac_driver.h"

#include "mdi/instance.h"

#include <stddef.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#   define DAC_DRIVER_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#   define DAC_DRIVER_ALWAYS_INLINE static inline
#endif

static const uint8_t s_chSineTable[DAC_DRIVER_SINE_TABLE_SIZE] = {
    128U, 131U, 134U, 137U, 140U, 143U, 146U, 149U, 152U, 155U, 158U, 162U, 165U, 167U, 170U, 173U,
    176U, 179U, 182U, 185U, 188U, 190U, 193U, 196U, 198U, 201U, 203U, 206U, 208U, 211U, 213U, 215U,
    218U, 220U, 222U, 224U, 226U, 228U, 230U, 232U, 234U, 235U, 237U, 238U, 240U, 241U, 243U, 244U,
    245U, 246U, 248U, 249U, 250U, 250U, 251U, 252U, 253U, 253U, 254U, 254U, 254U, 255U, 255U, 255U,
    255U, 255U, 255U, 255U, 254U, 254U, 254U, 253U, 253U, 252U, 251U, 250U, 250U, 249U, 248U, 246U,
    245U, 244U, 243U, 241U, 240U, 238U, 237U, 235U, 234U, 232U, 230U, 228U, 226U, 224U, 222U, 220U,
    218U, 215U, 213U, 211U, 208U, 206U, 203U, 201U, 198U, 196U, 193U, 190U, 188U, 185U, 182U, 179U,
    176U, 173U, 170U, 167U, 165U, 162U, 158U, 155U, 152U, 149U, 146U, 143U, 140U, 137U, 134U, 131U,
    128U, 124U, 121U, 118U, 115U, 112U, 109U, 106U, 103U, 100U, 97U, 93U, 90U, 88U, 85U, 82U,
    79U, 76U, 73U, 70U, 67U, 65U, 62U, 59U, 57U, 54U, 52U, 49U, 47U, 44U, 42U, 40U,
    37U, 35U, 33U, 31U, 29U, 27U, 25U, 23U, 21U, 20U, 18U, 17U, 15U, 14U, 12U, 11U,
    10U, 9U, 7U, 6U, 5U, 5U, 4U, 3U, 2U, 2U, 1U, 1U, 1U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 1U, 1U, 1U, 2U, 2U, 3U, 4U, 5U, 5U, 6U, 7U, 9U,
    10U, 11U, 12U, 14U, 15U, 17U, 18U, 20U, 21U, 23U, 25U, 27U, 29U, 31U, 33U, 35U,
    37U, 40U, 42U, 44U, 47U, 49U, 52U, 54U, 57U, 59U, 62U, 65U, 67U, 70U, 73U, 76U,
    79U, 82U, 85U, 88U, 90U, 93U, 97U, 100U, 103U, 106U, 109U, 112U, 115U, 118U, 121U, 124U,
};

static bool _dac_driver_IsReady(const dac_driver_t *ptThis)
{
    return (ptThis != NULL)
        && (ptThis->eState != DAC_DRIVER_STATE_UNINITIALIZED)
        && (ptThis->eState != DAC_DRIVER_STATE_ERROR);
}

static dac_driver_result_t _dac_driver_SetFrequency(
    dac_driver_t *ptThis,
    dac_driver_wave_t *ptWave,
    uint32_t wFrequencyHz)
{
    uint32_t wPrimask;

    if ((ptThis == NULL) || (ptWave == NULL)) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (!_dac_driver_IsReady(ptThis)) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }
    if (wFrequencyHz > DAC_DRIVER_MAX_FREQUENCY_HZ) {
        return DAC_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    wPrimask = __get_PRIMASK();
    __disable_irq();
    ptWave->wPhaseStepPerSample = (uint32_t)(
        ((uint64_t)wFrequencyHz << 32)
        / ptThis->wSampleFrequencyHz);
    ptWave->wPhase = 0U;
    ptWave->bValueValid = false;
    if (wPrimask == 0U) {
        __enable_irq();
    }
    return DAC_DRIVER_RESULT_OK;
}

DAC_DRIVER_ALWAYS_INLINE void _dac_driver_FastWriteCarrier(
    uint8_t chValue)
{
    MDI_IO_Write(dac_ab, 1U);
    MDI_IO_Write(dac_parallel, chValue);
    MDI_IO_Write(dac_wr, 0U);
    MDI_IO_Write(dac_wr, 1U);
}

DAC_DRIVER_ALWAYS_INLINE void _dac_driver_FastWriteEnvelope(
    uint8_t chValue)
{
    MDI_IO_Write(dac_ab, 0U);
    MDI_IO_Write(dac_parallel, chValue);
    MDI_IO_Write(dac_wr, 0U);
    MDI_IO_Write(dac_wr, 1U);
}

void dac_driver_IsrStep(dac_driver_t *ptThis)
{
    uint8_t chCarrierValue;
    uint8_t chEnvelopeValue;

    if ((ptThis == NULL) || (ptThis->eState != DAC_DRIVER_STATE_RUNNING)) {
        return;
    }

    ptThis->tCarrier.wPhase += ptThis->tCarrier.wPhaseStepPerSample;
    ptThis->tEnvelope.wPhase += ptThis->tEnvelope.wPhaseStepPerSample;
    chCarrierValue = s_chSineTable[
        (uint8_t)(ptThis->tCarrier.wPhase >> 24)];
    chEnvelopeValue = s_chSineTable[
        (uint8_t)(ptThis->tEnvelope.wPhase >> 24)];

    // 设置载波chCarrierValue
    if (!ptThis->tCarrier.bValueValid
        || (ptThis->tCarrier.chLastValue != chCarrierValue)) {
        _dac_driver_FastWriteCarrier(chCarrierValue);
        ptThis->tCarrier.chLastValue = chCarrierValue;
        ptThis->tCarrier.bValueValid = true;
    }
    // 设置包络波chEnvelopeValue
    if (!ptThis->tEnvelope.bValueValid
        || (ptThis->tEnvelope.chLastValue != chEnvelopeValue)) {
        _dac_driver_FastWriteEnvelope(chEnvelopeValue);
        ptThis->tEnvelope.chLastValue = chEnvelopeValue;
        ptThis->tEnvelope.bValueValid = true;
    }
}

dac_driver_result_t dac_driver_Init(
    dac_driver_t *ptThis,
    const dac_driver_cfg_t *ptCfg)
{
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }

    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = DAC_DRIVER_STATE_UNINITIALIZED;
    if ((ptCfg == NULL) || (ptCfg->wSampleFrequencyHz == 0U)) {
        ptThis->eState = DAC_DRIVER_STATE_ERROR;
        return DAC_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    MDI_IO_Write(dac_wr, 1U);
    MDI_IO_Write(dac_ab, 0U);
    MDI_IO_Write(dac_parallel, s_chSineTable[0]);
    MDI_IO_Write(dac_oe, 1U);
    if (MDI_TIMER_SetFrequency(dac_sample_timer,
            ptCfg->wSampleFrequencyHz) != MDI_OK) {
        ptThis->eState = DAC_DRIVER_STATE_ERROR;
        return DAC_DRIVER_RESULT_IO;
    }
    ptThis->wSampleFrequencyHz = ptCfg->wSampleFrequencyHz;
    ptThis->eState = DAC_DRIVER_STATE_IDLE;
    return DAC_DRIVER_RESULT_OK;
}

dac_driver_result_t dac_driver_SetCarrierFrequency(
    dac_driver_t *ptThis,
    uint32_t wFrequencyHz)
{
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    return _dac_driver_SetFrequency(ptThis, &ptThis->tCarrier,
                                    wFrequencyHz);
}

dac_driver_result_t dac_driver_SetEnvelopeFrequency(
    dac_driver_t *ptThis,
    uint32_t wFrequencyHz)
{
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    return _dac_driver_SetFrequency(ptThis, &ptThis->tEnvelope,
                                    wFrequencyHz);
}

dac_driver_result_t dac_driver_Start(dac_driver_t *ptThis)
{
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (!_dac_driver_IsReady(ptThis)) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }
    if (ptThis->eState == DAC_DRIVER_STATE_RUNNING) {
        return DAC_DRIVER_RESULT_OK;
    }
    MDI_IO_Write(dac_oe, 0U);
    if (MDI_TIMER_Start(dac_sample_timer) != MDI_OK) {
        MDI_IO_Write(dac_oe, 1U);
        ptThis->eState = DAC_DRIVER_STATE_ERROR;
        return DAC_DRIVER_RESULT_IO;
    }
    ptThis->eState = DAC_DRIVER_STATE_RUNNING;
    return DAC_DRIVER_RESULT_OK;
}

dac_driver_result_t dac_driver_Stop(dac_driver_t *ptThis)
{
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (!_dac_driver_IsReady(ptThis)) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }
    if (ptThis->eState == DAC_DRIVER_STATE_RUNNING) {
        if (MDI_TIMER_Stop(dac_sample_timer) != MDI_OK) {
            ptThis->eState = DAC_DRIVER_STATE_ERROR;
            return DAC_DRIVER_RESULT_IO;
        }
        ptThis->eState = DAC_DRIVER_STATE_IDLE;
    }
    MDI_IO_Write(dac_oe, 1U);
    return DAC_DRIVER_RESULT_OK;
}
