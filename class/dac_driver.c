/**
 * @file dac_driver.c
 * @brief TLC7528 dual-channel sine-wave DAC driver.
 * @author Modus project
 * @date 2026-10-08
 */
// A 通道（DACA）做低频包络（Envelope）
// B 通道（DACB）做高频载波（Carrier），支持调整载波幅值。

#include "dac_driver.h"

#include "mdi/instance.h"
#include "perf_counter.h"

#include <stddef.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#   define DAC_DRIVER_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#   define DAC_DRIVER_ALWAYS_INLINE static inline
#endif

static const uint8_t s_chSineTable[DAC_DRIVER_SINE_TABLE_SIZE] = {
    128U, 131U, 134U, 137U, 140U, 143U, 146U, 149U, 152U, 155U, 158U, 162U,
    165U, 167U, 170U, 173U, 176U, 179U, 182U, 185U, 188U, 190U, 193U, 196U,
    198U, 201U, 203U, 206U, 208U, 211U, 213U, 215U, 218U, 220U, 222U, 224U,
    226U, 228U, 230U, 232U, 234U, 235U, 237U, 238U, 240U, 241U, 243U, 244U,
    245U, 246U, 248U, 249U, 250U, 250U, 251U, 252U, 253U, 253U, 254U, 254U,
    254U, 255U, 255U, 255U, 255U, 255U, 255U, 255U, 254U, 254U, 254U, 253U,
    253U, 252U, 251U, 250U, 250U, 249U, 248U, 246U, 245U, 244U, 243U, 241U,
    240U, 238U, 237U, 235U, 234U, 232U, 230U, 228U, 226U, 224U, 222U, 220U,
    218U, 215U, 213U, 211U, 208U, 206U, 203U, 201U, 198U, 196U, 193U, 190U,
    188U, 185U, 182U, 179U, 176U, 173U, 170U, 167U, 165U, 162U, 158U, 155U,
    152U, 149U, 146U, 143U, 140U, 137U, 134U, 131U, 128U, 124U, 121U, 118U,
    115U, 112U, 109U, 106U, 103U, 100U, 97U, 93U, 90U, 88U, 85U, 82U,
    79U, 76U, 73U, 70U, 67U, 65U, 62U, 59U, 57U, 54U, 52U, 49U,
    47U, 44U, 42U, 40U, 37U, 35U, 33U, 31U, 29U, 27U, 25U, 23U,
    21U, 20U, 18U, 17U, 15U, 14U, 12U, 11U, 10U, 9U, 7U, 6U,
    5U, 5U, 4U, 3U, 2U, 2U, 1U, 1U, 1U, 0U, 0U, 0U,
    0U, 0U, 0U, 0U, 1U, 1U, 1U, 2U, 2U, 3U, 4U, 5U,
    5U, 6U, 7U, 9U, 10U, 11U, 12U, 14U, 15U, 17U, 18U, 20U,
    21U, 23U, 25U, 27U, 29U, 31U, 33U, 35U, 37U, 40U, 42U, 44U,
    47U, 49U, 52U, 54U, 57U, 59U, 62U, 65U, 67U, 70U, 73U, 76U,
    79U, 82U, 85U, 88U, 90U, 93U, 97U, 100U, 103U, 106U, 109U, 112U,
    115U, 118U, 121U, 124U,

};

/**
 * @brief Build one amplitude-scaled carrier/envelope table outside the ISR.
 * @param ptThis DAC driver instance receiving the prepared table.
 * @param chTable Table index to build.
 * @param chAmplitudePercent Peak amplitude in percent (valley fixed at 0).
 * @return None.
 */
static void _dac_driver_BuildCarrierTable(
    dac_driver_t *ptThis,
    uint8_t chTable,
    uint8_t chAmplitudePercent)
{
    uint16_t hwIndex = 0U;
    for (hwIndex = 0U; hwIndex < DAC_DRIVER_SINE_TABLE_SIZE; ++hwIndex) {
        const int32_t wBaseValue = (int32_t)s_chSineTable[hwIndex];
        const int32_t wScaledValue = (wBaseValue
            * (int32_t)chAmplitudePercent)
                / (int32_t)DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT;
        ptThis->aachCarrierTable[chTable][hwIndex] = (uint8_t)wScaledValue;
    }
}

/**
 * @brief Check whether the driver accepts control operations.
 * @param ptThis Caller-owned module instance.
 * @return True when the requested check succeeds.
 */
static bool _dac_driver_IsReady(const dac_driver_t *ptThis)
{
    return (ptThis != NULL)
        && (ptThis->eState != DAC_DRIVER_STATE_UNINITIALIZED)
        && (ptThis->eState != DAC_DRIVER_STATE_ERROR);
}

/**
 * @brief Validate Hz and publish a coherent phase increment.
 * @param ptThis Caller-owned module instance.
 * @param ptWave Waveform owned by this DAC instance.
 * @param wFrequencyHz Frequency in Hz; zero holds the midpoint, maximum 20 kHz.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
static dac_driver_result_t _dac_driver_CalculatePhaseStep(
    const dac_driver_t *ptThis,
    uint32_t wFrequencyHz,
    uint32_t *pwPhaseStep)
{
    if ((ptThis == NULL) || (pwPhaseStep == NULL)) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (!_dac_driver_IsReady(ptThis)) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }
    if ((wFrequencyHz > DAC_DRIVER_MAX_FREQUENCY_HZ)
        || (wFrequencyHz >= ptThis->wSampleFrequencyHz / 2U)) {
        return DAC_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    /* Division must not block the high-frequency ISR. */
    *pwPhaseStep = (uint32_t)(((uint64_t)wFrequencyHz << 32)
                             / ptThis->wSampleFrequencyHz);
    return DAC_DRIVER_RESULT_OK;
}

/* TLC7528 parallel timing (144 MHz core, 1 cycle ~= 6.94 ns):
 * - tsu(DAC) >= 50 ns, tsu(D) >= 25 ns (including TXB0108 delay):
 *   8 NOPs (~56 ns) + GPIO write instruction overhead.
 * - tw(WR) low pulse >= 50 ns:
 *   9 NOPs (~63 ns) + GPIO register write overhead.
 * - th(DAC) / th(D) >= 10 ns:
 *   4 NOPs (~28 ns).
 */
#if defined(__arm__) || defined(__thumb__)
#   define DAC_DRIVER_SETUP_DELAY() \
        __asm__ volatile ("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop")
#   define DAC_DRIVER_PULSE_DELAY() \
        __asm__ volatile ("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop")
#   define DAC_DRIVER_HOLD_DELAY() \
        __asm__ volatile ("nop\nnop\nnop\nnop")
#else
#   define DAC_DRIVER_SETUP_DELAY() ((void)0)
#   define DAC_DRIVER_PULSE_DELAY() ((void)0)
#   define DAC_DRIVER_HOLD_DELAY()  ((void)0)
#endif

/**
 * @brief Latch one sample into the carrier DAC channel.
 * @param chValue Unsigned 8-bit DAC sample.
 * @return None.
 */
DAC_DRIVER_ALWAYS_INLINE void _dac_driver_FastWriteCarrier(
    uint8_t chValue)
{
    MDI_IO_Write(dac_ab, 1U);
    MDI_IO_Write(dac_parallel, chValue);
    DAC_DRIVER_SETUP_DELAY();
    MDI_IO_Write(dac_wr, 0U);
    DAC_DRIVER_PULSE_DELAY();
    MDI_IO_Write(dac_wr, 1U);
    DAC_DRIVER_HOLD_DELAY();
}

/**
 * @brief Latch one sample into the envelope DAC channel.
 * @param chValue Unsigned 8-bit DAC sample.
 * @return None.
 */
DAC_DRIVER_ALWAYS_INLINE void _dac_driver_FastWriteEnvelope(
    uint8_t chValue)
{
    MDI_IO_Write(dac_ab, 0U);
    MDI_IO_Write(dac_parallel, chValue);
    DAC_DRIVER_SETUP_DELAY();
    MDI_IO_Write(dac_wr, 0U);
    DAC_DRIVER_PULSE_DELAY();
    MDI_IO_Write(dac_wr, 1U);
    DAC_DRIVER_HOLD_DELAY();
}

/**
 * @brief Advance both waveforms by one hardware sample.
 * @param ptThis Caller-owned module instance.
 * @return None.
 * @note ISR-only; setters publish execution parameters atomically.
 */
void dac_driver_IsrStep(dac_driver_t *ptThis)
{
    uint8_t chCarrierValue = 0U;
    uint8_t chEnvelopeValue = 0U;
    uint8_t chCarrierTable = 0U;

    if ((ptThis == NULL) || (ptThis->eState != DAC_DRIVER_STATE_RUNNING)) {
        return;
    }

    ptThis->tCarrier.wPhase += ptThis->tCarrier.wPhaseStepPerSample;
    ptThis->tEnvelope.wPhase += ptThis->tEnvelope.wPhaseStepPerSample;
    chCarrierTable = ptThis->chActiveCarrierTable;
    chCarrierValue = ptThis->aachCarrierTable[chCarrierTable][
        (uint8_t)(ptThis->tCarrier.wPhase >> 24)];
    if (ptThis->eEnvelopeWaveType == DAC_DRIVER_WAVE_DIGITAL) {
        chEnvelopeValue = ((ptThis->tEnvelope.wPhase >> 31) == 0U) ? 255U : 0U;
    } else {
        chEnvelopeValue = (uint8_t)(255U - s_chSineTable[
            (uint8_t)(ptThis->tEnvelope.wPhase >> 24)]);
    }

    if (!ptThis->tCarrier.bValueValid
        || (ptThis->tCarrier.chLastValue != chCarrierValue)) {
        _dac_driver_FastWriteCarrier(chCarrierValue);
        ptThis->tCarrier.chLastValue = chCarrierValue;
        ptThis->tCarrier.bValueValid = true;
    }
    if (!ptThis->tEnvelope.bValueValid
        || (ptThis->tEnvelope.chLastValue != chEnvelopeValue)) {
        _dac_driver_FastWriteEnvelope(chEnvelopeValue);
        ptThis->tEnvelope.chLastValue = chEnvelopeValue;
        ptThis->tEnvelope.bValueValid = true;
    }
}

/**
 * @brief Validate the sample rate and leave DAC output disabled.
 * @param ptThis Caller-owned module instance.
 * @param ptCfg Initialization policy in caller-facing units.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dac_driver_result_t dac_driver_Init(
    dac_driver_t *ptThis,
    const dac_driver_cfg_t *ptCfg)
{
    mdi_status_t eStatus = MDI_OK;

    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }

    MDI_IO_Write(dac_oe, 0U);
    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = DAC_DRIVER_STATE_UNINITIALIZED;
    ptThis->eEnvelopeWaveType = DAC_DRIVER_WAVE_SINE;
    _dac_driver_BuildCarrierTable(ptThis, 0U,
        DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT);
    _dac_driver_BuildCarrierTable(ptThis, 1U,
        DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT);
    eStatus = MDI_TIMER_Stop(dac_sample_timer);
    if (eStatus != MDI_OK) {
        ptThis->eState = DAC_DRIVER_STATE_ERROR;
        return DAC_DRIVER_RESULT_IO;
    }

    if ((ptCfg == NULL) || (ptCfg->wSampleFrequencyHz == 0U)) {
        ptThis->eState = DAC_DRIVER_STATE_ERROR;
        return DAC_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    /* Enable TXB0108 translator to write mid-scale 128 into both channels,
     * establishing defined analog zero before output is muted. */
    MDI_IO_Write(dac_oe, 1U);
    MDI_IO_Write(dac_wr, 1U);
    _dac_driver_FastWriteCarrier(128U);
    _dac_driver_FastWriteEnvelope(128U);
    ptThis->tCarrier.chLastValue = 128U;
    ptThis->tCarrier.bValueValid = true;
    ptThis->tEnvelope.chLastValue = 128U;
    ptThis->tEnvelope.bValueValid = true;
    MDI_IO_Write(dac_oe, 0U);

    eStatus = MDI_TIMER_SetFrequency(dac_sample_timer,
                                    ptCfg->wSampleFrequencyHz);
    if (eStatus != MDI_OK) {
        ptThis->eState = DAC_DRIVER_STATE_ERROR;
        return DAC_DRIVER_RESULT_IO;
    }
    ptThis->wSampleFrequencyHz = ptCfg->wSampleFrequencyHz;
    ptThis->eState = DAC_DRIVER_STATE_IDLE;
    return DAC_DRIVER_RESULT_OK;
}

/**
 * @brief Validate and publish both DDS phase increments as one update.
 * @param ptThis Caller-owned module instance.
 * @param wCarrierFrequencyHz DACB/carrier frequency in Hz.
 * @param wEnvelopeFrequencyHz DACA/envelope frequency in Hz.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dac_driver_result_t dac_driver_SetFrequencies(
    dac_driver_t *ptThis,
    uint32_t wCarrierFrequencyHz,
    uint32_t wEnvelopeFrequencyHz)
{
    uint32_t wCarrierPhaseStep = 0U;
    uint32_t wEnvelopePhaseStep = 0U;
    dac_driver_result_t eResult = DAC_DRIVER_RESULT_OK;

    eResult = _dac_driver_CalculatePhaseStep(ptThis,
        wCarrierFrequencyHz, &wCarrierPhaseStep);
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return eResult;
    }
    eResult = _dac_driver_CalculatePhaseStep(ptThis,
        wEnvelopeFrequencyHz, &wEnvelopePhaseStep);
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return eResult;
    }

    safe_atom_code() {
        ptThis->tCarrier.wPhaseStepPerSample = wCarrierPhaseStep;
        ptThis->tCarrier.wPhase = 0U;
        ptThis->tCarrier.bValueValid = false;
        ptThis->tEnvelope.wPhaseStepPerSample = wEnvelopePhaseStep;
        ptThis->tEnvelope.wPhase = 0U;
        ptThis->tEnvelope.bValueValid = false;
    }
    return DAC_DRIVER_RESULT_OK;
}

dac_driver_result_t dac_driver_SetCarrierAmplitude(
    dac_driver_t *ptThis,
    uint8_t chAmplitudePercent)
{
    uint8_t chInactiveTable = 0U;

    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (!_dac_driver_IsReady(ptThis)) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }
    if (chAmplitudePercent > DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT) {
        return DAC_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    chInactiveTable = (uint8_t)(ptThis->chActiveCarrierTable ^ 1U);
    _dac_driver_BuildCarrierTable(ptThis, chInactiveTable,
                                   chAmplitudePercent);
    safe_atom_code() {
        ptThis->chActiveCarrierTable = chInactiveTable;
        ptThis->tCarrier.bValueValid = false;
    }
    return DAC_DRIVER_RESULT_OK;
}

/**
 * @brief Configure the envelope waveform type (sine or digital square).
 * @param ptThis Caller-owned module instance.
 * @param eWaveType Envelope wave mode.
 * @return Module result code; invalid inputs leave the mode unchanged.
 */
dac_driver_result_t dac_driver_SetWaveType(
    dac_driver_t *ptThis,
    dac_driver_wave_type_t eWaveType)
{
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (!_dac_driver_IsReady(ptThis)) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }
    if ((eWaveType != DAC_DRIVER_WAVE_SINE)
        && (eWaveType != DAC_DRIVER_WAVE_DIGITAL)) {
        return DAC_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    safe_atom_code() {
        ptThis->eEnvelopeWaveType = eWaveType;
        ptThis->tEnvelope.bValueValid = false;
    }
    return DAC_DRIVER_RESULT_OK;
}

/**
 * @brief Enable DAC output and start the configured sample timer.
 * @param ptThis Caller-owned module instance.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dac_driver_result_t dac_driver_Start(dac_driver_t *ptThis)
{
    mdi_status_t eStatus = MDI_OK;
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (!_dac_driver_IsReady(ptThis)) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }
    if (ptThis->eState == DAC_DRIVER_STATE_RUNNING) {
        return DAC_DRIVER_RESULT_OK;
    }
    /* Publish state before the timer can enter its ISR. */
    ptThis->eState = DAC_DRIVER_STATE_RUNNING;
    MDI_IO_Write(dac_oe, 1U);
    eStatus = MDI_TIMER_Start(dac_sample_timer);
    if (eStatus != MDI_OK) {
        MDI_IO_Write(dac_oe, 0U);
        ptThis->eState = DAC_DRIVER_STATE_ERROR;
    }
    if (eStatus != MDI_OK) {
        return DAC_DRIVER_RESULT_IO;
    }
    return DAC_DRIVER_RESULT_OK;
}

/**
 * @brief Disable output and stop the timer, preserving latched faults.
 * @param ptThis Caller-owned module instance.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dac_driver_result_t dac_driver_Stop(dac_driver_t *ptThis)
{
    mdi_status_t eStatus = MDI_OK;
    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    /* Safe output is required even if the timer stop fails. */
    safe_atom_code() {
        eStatus = MDI_TIMER_Stop(dac_sample_timer);
        /* Ensure translator is active to write zero-offset mid-scale 128,
         * preventing DC bias leakage into op-amp and power amp stages. */
        MDI_IO_Write(dac_oe, 1U);
        _dac_driver_FastWriteCarrier(128U);
        _dac_driver_FastWriteEnvelope(128U);
        ptThis->tCarrier.chLastValue = 128U;
        ptThis->tCarrier.bValueValid = true;
        ptThis->tEnvelope.chLastValue = 128U;
        ptThis->tEnvelope.bValueValid = true;
        MDI_IO_Write(dac_oe, 0U);
        if (eStatus != MDI_OK) {
            ptThis->eState = DAC_DRIVER_STATE_ERROR;
        } else if (ptThis->eState != DAC_DRIVER_STATE_ERROR) {
            ptThis->eState = DAC_DRIVER_STATE_IDLE;
        } else {
            /* Stop preserves a latched fault. */
        }
    }
    return eStatus == MDI_OK ? DAC_DRIVER_RESULT_OK : DAC_DRIVER_RESULT_IO;
}
