/**
 * @file mircoelec.c
 * @brief Micro-electronics control Class skeleton.
 * @author Modus project
 * @date 2026-10-08
 */

#include "mircoelec.h"

#include "mdi/instance.h"
#include "mdebug/mshell.h"
#include "mdebug/util_debug.h"

#include <stddef.h>
#include <string.h>

#define MIRCROELEC_DAC_SAMPLE_FREQUENCY_HZ 200000U

static modus_base_t s_tMircoelecBase;
static modus_base_cfg_t s_tMircoelecBaseCfg = {
    .wId = MIRCROELEC,
    .wParent = 0U,
    .FcnInterface = {
        .Clock = mircoelec_Clock,
        .Run = mircoelec_Run,
    },
};

extern mircoelec_t tMircoelec;

/**
 * @brief Initialize the DAC disabled and register after dependencies succeed.
 * @param wObjectAddr Address of the MODUS Class instance.
 * @param wObjectCfgAddr Address of the Class initialization configuration.
 * @return MODUS_SUCCESS on success; otherwise a MODUS error code.
 */
int mircoelec_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr)
{
    mircoelec_t *ptThis = (mircoelec_t *)wObjectAddr;
    const mircoelec_cfg_t *ptCfg =
        (const mircoelec_cfg_t *)wObjectCfgAddr;
    int nResult = MODUS_EFAIL;
    dac_driver_result_t eStop = DAC_DRIVER_RESULT_OK;

    if ((ptThis == NULL) || (ptCfg == NULL)) {
        return MODUS_EFAIL;
    }

    MDI_IO_Write(fan, 0U);
    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->ptBase = &s_tMircoelecBase;
    ptThis->eState = MIRCROELEC_STATE_UNINITIALIZED;

    ptThis->eDacError = dac_driver_Init(&ptThis->tDac, &ptCfg->tDacCfg);
    if (ptThis->eDacError != DAC_DRIVER_RESULT_OK) {
        ptThis->eState = MIRCROELEC_STATE_ERROR;
        return MODUS_EFAIL;
    }

    s_tMircoelecBaseCfg.wParent = wObjectAddr;
    nResult = mbase_Init(ptThis->ptBase, &s_tMircoelecBaseCfg);
    if (nResult != MODUS_SUCCESS) {
        eStop = dac_driver_Stop(&ptThis->tDac);
        ptThis->eDacError = (eStop == DAC_DRIVER_RESULT_OK)
                         ? DAC_DRIVER_RESULT_FAULT : eStop;
        ptThis->eState = MIRCROELEC_STATE_ERROR;
        return nResult;
    }
    ptThis->eState = MIRCROELEC_STATE_READY;
#if MWAVEFORM_ENABLE
    mircoelec_waveform_Init(&ptThis->tWaveform,
                            ptCfg->tDacCfg.wSampleFrequencyHz);
#endif
    return MODUS_SUCCESS;
}

/**
 * @brief Latch a DAC control failure in the owning Class.
 * @param ptThis Initialized Class instance.
 * @param eError Detailed DAC driver result.
 * @return The supplied driver result.
 */
static dac_driver_result_t _mircoelec_LatchDacError(
    mircoelec_t *ptThis,
    dac_driver_result_t eError)
{
    MDI_IO_Write(fan, 0U);
    ptThis->eDacError = eError;
    ptThis->eState = MIRCROELEC_STATE_ERROR;
#if MWAVEFORM_ENABLE
    mircoelec_waveform_Stop(&ptThis->tWaveform);
#endif
    return eError;
}

/**
 * @brief Set both DAC frequencies and enable waveform output.
 * @param ptThis Initialized Class instance.
 * @param eWaveType Envelope wave mode (sine or digital square).
 * @param wAHz DACA/envelope frequency in Hz.
 * @param wBHz DACB/carrier frequency in Hz.
 * @return DAC driver result; invalid frequencies leave both settings intact.
 */
dac_driver_result_t mircoelec_StartWaveform(
    mircoelec_t *ptThis,
    dac_driver_wave_type_t eWaveType,
    uint32_t wAHz,
    uint32_t wBHz)
{
    dac_driver_result_t eResult = DAC_DRIVER_RESULT_OK;

    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (ptThis->eState != MIRCROELEC_STATE_READY) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }

    eResult = dac_driver_SetWaveType(&ptThis->tDac, eWaveType);
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return eResult;
    }
    eResult = dac_driver_SetFrequencies(&ptThis->tDac, wBHz, wAHz);
    if (eResult == DAC_DRIVER_RESULT_INVALID_ARGUMENT) {
        return eResult;
    }
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return _mircoelec_LatchDacError(ptThis, eResult);
    }
    eResult = dac_driver_Start(&ptThis->tDac);
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return _mircoelec_LatchDacError(ptThis, eResult);
    }
    MDI_IO_Write(fan, 1U);
    ptThis->eDacError = DAC_DRIVER_RESULT_OK;
#if MWAVEFORM_ENABLE
    mircoelec_waveform_Start(&ptThis->tWaveform, wAHz, wBHz);
#endif
    return DAC_DRIVER_RESULT_OK;
}

/**
 * @brief Disable DAC output and stop the owned sample timer.
 * @param ptThis Initialized Class instance.
 * @return DAC driver result; OE remains disabled if timer stop fails.
 */
dac_driver_result_t mircoelec_StopWaveform(mircoelec_t *ptThis)
{
    dac_driver_result_t eResult = DAC_DRIVER_RESULT_OK;

    if (ptThis == NULL) {
        return DAC_DRIVER_RESULT_NULL;
    }
    if (ptThis->tDac.eState == DAC_DRIVER_STATE_UNINITIALIZED) {
        return DAC_DRIVER_RESULT_NOT_READY;
    }

#if MWAVEFORM_ENABLE
    mircoelec_waveform_Stop(&ptThis->tWaveform);
#endif
    eResult = dac_driver_Stop(&ptThis->tDac);
    MDI_IO_Write(fan, 0U);
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return _mircoelec_LatchDacError(ptThis, eResult);
    }
    return DAC_DRIVER_RESULT_OK;
}

/**
 * @brief Advance the DAC owned by the declared single master instance.
 * @return None.
 */
void mircoelec_IsrStep(void)
{
    dac_driver_IsrStep(&tMircoelec.tDac);
#if MWAVEFORM_ENABLE
    mircoelec_waveform_IsrStep(&tMircoelec.tWaveform, &tMircoelec.tDac);
#endif
}

/**
 * @brief Validate the Class required foreground callback.
 * @param wObjectAddr Address of the MODUS Class instance.
 * @return MODUS_SUCCESS on success; otherwise a MODUS error code.
 */
int mircoelec_Run(uintptr_t wObjectAddr)
{
    mircoelec_t *ptThis = (mircoelec_t *)wObjectAddr;

    if ((ptThis == NULL) || (ptThis->eState != MIRCROELEC_STATE_READY)) {
        return MODUS_EFAIL;
    }
    /* MODUS 当前要求每个对象提供 Run；DAC 波形由硬件 TMR1 ISR 推进。 */
    return MODUS_SUCCESS;
}

/**
 * @brief Validate the Class required fixed-rate callback.
 * @param wObjectAddr Address of the MODUS Class instance.
 * @return MODUS_SUCCESS on success; otherwise a MODUS error code.
 */
int mircoelec_Clock(uintptr_t wObjectAddr)
{
    mircoelec_t *ptThis = (mircoelec_t *)wObjectAddr;

    if ((ptThis == NULL) || (ptThis->eState != MIRCROELEC_STATE_READY)) {
        return MODUS_EFAIL;
    }

    return MODUS_SUCCESS;
}

#if MSHELL_ENABLE
/**
 * @brief Parse and execute one DAC MShell command.
 * @param pchArgs Text after the dac command name.
 * @return None.
 */
static void _mircoelec_DacCommand(const char *pchArgs)
{
    const char *pchText = pchArgs;
    uint32_t wAHz = 0U;
    uint32_t wBHz = 0U;
    uint32_t wBAmplitudePercent = DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT;
    dac_driver_result_t eResult = DAC_DRIVER_RESULT_OK;

    if (mshell_ConsumeWord(&pchText, "start")) {
        dac_driver_wave_type_t eWaveType = DAC_DRIVER_WAVE_SINE;
        if (mshell_ConsumeWord(&pchText, "dig")) {
            eWaveType = DAC_DRIVER_WAVE_DIGITAL;
        } else if (mshell_ConsumeWord(&pchText, "sin")) {
            eWaveType = DAC_DRIVER_WAVE_SINE;
        } else {
            eWaveType = DAC_DRIVER_WAVE_SINE;
        }

        if ((mshell_Parse(&pchText, &wAHz) != MSHELL_PARSE_OK)
            || (mshell_Parse(&pchText, &wBHz) != MSHELL_PARSE_OK)
            || (wAHz > DAC_DRIVER_MAX_FREQUENCY_HZ)
            || (wBHz > DAC_DRIVER_MAX_FREQUENCY_HZ)) {
            MLOGF(W,
                "Usage: dac start [dig|sin] <A_Hz> <B_Hz> [B_percent]\r\n");
            return;
        }
        if (!mshell_AtEnd(pchText)
            && (mshell_Parse(&pchText, &wBAmplitudePercent)
                != MSHELL_PARSE_OK)) {
            MLOGF(W,
                "Usage: dac start [dig|sin] <A_Hz> <B_Hz> [B_percent]\r\n");
            return;
        }
        if (!mshell_AtEnd(pchText)
            || (wBAmplitudePercent
                > DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT)) {
            MLOGF(W,
                "Usage: dac start [dig|sin] <A_Hz> <B_Hz> [B_percent]\r\n");
            return;
        }
        eResult = dac_driver_SetCarrierAmplitude(&tMircoelec.tDac,
            (uint8_t)wBAmplitudePercent);
        if (eResult != DAC_DRIVER_RESULT_OK) {
            MLOGF(W, "DAC start failed: %d\r\n", (int)eResult);
            return;
        }
        eResult = mircoelec_StartWaveform(&tMircoelec, eWaveType, wAHz, wBHz);
        if (eResult == DAC_DRIVER_RESULT_OK) {
            MLOGF(I, "DAC started: mode=%s A=%lu Hz B=%lu Hz B_amp=%lu%%\r\n",
                  (eWaveType == DAC_DRIVER_WAVE_DIGITAL) ? "dig" : "sin",
                  (unsigned long)wAHz, (unsigned long)wBHz,
                  (unsigned long)wBAmplitudePercent);
        } else {
            MLOGF(W, "DAC start failed: %d\r\n", (int)eResult);
        }
        return;
    }

    if (mshell_ConsumeWord(&pchText, "stop")
        && mshell_AtEnd(pchText)) {
        eResult = mircoelec_StopWaveform(&tMircoelec);
        if (eResult == DAC_DRIVER_RESULT_OK) {
            MLOGF(I, "DAC output stopped\r\n");
        } else {
            MLOGF(W, "DAC stop failed: %d\r\n", (int)eResult);
        }
        return;
    }
    MLOGF(W, "Usage: dac start [dig|sin] <A_Hz> <B_Hz> | dac stop\r\n");
}

MODUS_SHELL_CMD(dac, _mircoelec_DacCommand,
                "DAC waveform: start [dig|sin] <A_Hz> <B_Hz> [B_percent] | stop");
#endif

MODUS_DECLARE_OBJECT(mircoelec, Mircoelec,
    .tDacCfg = {
        .wSampleFrequencyHz = MIRCROELEC_DAC_SAMPLE_FREQUENCY_HZ,
    }
)
