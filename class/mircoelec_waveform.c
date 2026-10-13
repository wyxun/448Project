/**
 * @file mircoelec_waveform.c
 * @brief Synchronous DAC telemetry and normalized ideal-AM simulation.
 * @author Modus project
 * @date 2026-10-13
 */
#include "modus.h"
#include "mircoelec_waveform.h"

#if MWAVEFORM_ENABLE
#include "mdebug/mwaveform.h"
#include "mdebug/util_debug.h"
#include "perf_counter.h"
#include <stddef.h>
#include <string.h>

#define MIRCOELEC_WAVEFORM_RATE_HZ 10000U
#define MIRCOELEC_WAVEFORM_PERIOD_NS 100000U

/**
 * @brief Rebuild this module's stream to discard data from earlier runs.
 * @param ptThis Class-owned diagnostic state with capture disabled.
 * @return True when all channels are registered successfully.
 * @note Foreground only; Init clears pending batches and the RTT up buffer.
 */
static bool _mircoelec_waveform_Configure(mircoelec_waveform_t *ptThis)
{
    static const char s_achNames[3][8] = {"EnvA", "CarrB", "AMSim"};
    static const float s_afScales[3] = {255.0f, 255.0f, 32512.5f};
    int nResult = mwaveform.Init(NULL);

    if (nResult != MODUS_SUCCESS) {
        return false;
    }
    mwaveform.SetRate(0U);
    mwaveform.SetDecimation(1U);
    mwaveform.SetSamplePeriodNs(MIRCOELEC_WAVEFORM_PERIOD_NS);
    for (uint8_t chIndex = 0U; chIndex < 3U; chIndex++) {
        ptThis->ahwSamples[chIndex] = 0;
        ptThis->achChannels[chIndex] = mwaveform.AddVariable(
            s_achNames[chIndex], s_afScales[chIndex],
            &ptThis->ahwSamples[chIndex], MWAVEFORM_VAR_RAW);
        if (ptThis->achChannels[chIndex] == UINT8_MAX) {
            return false;
        }
    }
    return true;
}

void mircoelec_waveform_Init(mircoelec_waveform_t *ptThis,
                            uint32_t wSampleFrequencyHz)
{
    if (ptThis == NULL) {
        return;
    }
    (void)memset(ptThis, 0, sizeof(*ptThis));
    if ((wSampleFrequencyHz < MIRCOELEC_WAVEFORM_RATE_HZ)
        || ((wSampleFrequencyHz % MIRCOELEC_WAVEFORM_RATE_HZ) != 0U)) {
        MLOGF(W, "DAC waveform disabled: invalid sample rate\r\n");
        return;
    }
    ptThis->wSampleDivider = wSampleFrequencyHz / MIRCOELEC_WAVEFORM_RATE_HZ;
    if (!_mircoelec_waveform_Configure(ptThis)) {
        MLOGF(W, "DAC waveform disabled: channel setup failed\r\n");
    }
}

void mircoelec_waveform_Start(mircoelec_waveform_t *ptThis,
                             uint32_t wAHz, uint32_t wBHz)
{
    if ((ptThis == NULL) || (ptThis->wSampleDivider == 0U)) {
        return;
    }
    mircoelec_waveform_Stop(ptThis);
    /* Rebuilding buffers runs in the foreground with capture disabled;
     * holding off the 320 kHz DAC ISR for this work would distort output. */
    if (!_mircoelec_waveform_Configure(ptThis)) {
        MLOGF(W, "DAC waveform disabled: channel setup failed\r\n");
        return;
    }
    safe_atom_code() {
        ptThis->wSampleCount = 0U;
        mwaveform.Start();
        ptThis->bRunning = true;
    }
    MLOGF(I, "DAC waveform: EnvA/CarrB/AMSim at 10000 Hz (ideal AM)\r\n");
    if (((uint64_t)wAHz + (uint64_t)wBHz)
        >= MIRCOELEC_WAVEFORM_RATE_HZ / 2U) {
        MLOGF(W, "DAC waveform aliasing: A+B must be below 5000 Hz\r\n");
    }
}

void mircoelec_waveform_Stop(mircoelec_waveform_t *ptThis)
{
    if (ptThis == NULL) {
        return;
    }
    safe_atom_code() {
        ptThis->bRunning = false;
    }
    mwaveform.Stop();
}

void mircoelec_waveform_IsrStep(mircoelec_waveform_t *ptThis,
                               const dac_driver_t *ptDac)
{
    uint32_t wSampleCount = 0U;
    int32_t wEnvelope = 0;
    int32_t wCarrier = 0;

    if ((ptThis == NULL) || (ptDac == NULL) || !ptThis->bRunning
        || (ptDac->eState != DAC_DRIVER_STATE_RUNNING)) {
        return;
    }
    wSampleCount = ptThis->wSampleCount + 1U;
    if (wSampleCount < ptThis->wSampleDivider) {
        ptThis->wSampleCount = wSampleCount;
        return;
    }
    ptThis->wSampleCount = 0U;
    /* Both values were latched by the same DAC ISR. No independent DDS
     * or floating-point math is needed to reproduce their ideal product. */
    wEnvelope = (int32_t)ptDac->tEnvelope.chLastValue;
    wCarrier = 2 * (int32_t)ptDac->tCarrier.chLastValue - 255;
    ptThis->ahwSamples[0] = (int16_t)wEnvelope;
    ptThis->ahwSamples[1] = (int16_t)wCarrier;
    /* Halving +/-65025 fits int16_t; the descriptor restores normalization.
     * Signed division preserves truncation toward zero for negative values. */
    ptThis->ahwSamples[2] = (int16_t)((wEnvelope * wCarrier) / 2);
    mwaveform.Step();
}
#endif
