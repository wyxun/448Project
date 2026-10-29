/**
 * @file mircoelec.c
 * @brief Micro-electronics control Class skeleton.
 * @author Modus project
 * @date 2026-10-08
 */

#include "mircoelec.h"

#include "mdi/instance.h"
#include "perf_counter.h"
#include "perfc_task_pt.h"

#include <stddef.h>
#include <string.h>

static modus_base_t s_tMircoelecBase;
static uint8_t s_achMircoelecMessages[128];
static modus_base_cfg_t s_tMircoelecBaseCfg = {
    .wId = MIRCROELEC,
    .pchRingBuffer = s_achMircoelecMessages,
    .hwRingSize = sizeof(s_achMircoelecMessages),
    .wParent = 0U,
    .FcnInterface = {
        .Clock = mircoelec_Clock,
        .Run = mircoelec_Run,
    },
};

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
    MDI_IO_Write(bio_power, 0U);
    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->ptBase = &s_tMircoelecBase;
    ptThis->eState = MIRCROELEC_STATE_UNINITIALIZED;

    if (heater_control_Init(&ptThis->tHeater, ptCfg->ptTemperature)
        != HEATER_CONTROL_RESULT_OK) {
        ptThis->eState = MIRCROELEC_STATE_ERROR;
        return MODUS_EFAIL;
    }

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

    eResult = dac_driver_Stop(&ptThis->tDac);
    MDI_IO_Write(fan, 0U);
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return _mircoelec_LatchDacError(ptThis, eResult);
    }
    return DAC_DRIVER_RESULT_OK;
}

dac_driver_result_t mircoelec_StartConfiguredWaveform(
    mircoelec_t *ptThis, dac_driver_wave_type_t eWaveType,
    uint32_t wAHz, uint32_t wBHz, uint8_t chAmplitudePercent)
{
    dac_driver_result_t eResult = dac_driver_SetCarrierAmplitude(
        &ptThis->tDac, chAmplitudePercent);
    if (eResult != DAC_DRIVER_RESULT_OK) {
        return eResult;
    }
    return mircoelec_StartWaveform(ptThis, eWaveType, wAHz, wBHz);
}

/** Read one complete BIO command into the persistent class state. */
static bool _mircoelec_ReceiveBioCommand(mircoelec_t *ptThis)
{
    int nLength = mbase_MessagePendFromRing(ptThis->ptBase,
        (uint8_t *)&ptThis->tBioCommand,
        (uint16_t)sizeof(ptThis->tBioCommand));
    return nLength == (int)sizeof(ptThis->tBioCommand);
}

/**
 * @brief Advance the BIO control protothread.
 * @param ptThis Initialized mircoelec instance.
 * @return PT progress or error.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
static fsm_rt_t _mircoelec_RunBio(mircoelec_t *ptThis)
{
    dac_driver_result_t eResult = DAC_DRIVER_RESULT_OK;

    PERFC_PT_BEGIN(ptThis->chBioPt)
    while (1) {
        PERFC_PT_WAIT_UNTIL(_mircoelec_ReceiveBioCommand(ptThis))

        if (ptThis->tBioCommand.eCommand == MIRCROELEC_COMMAND_STOP) {
            MDI_IO_Write(bio_power, 0U);
            MDI_IO_Write(fan, 0U);
            ptThis->eBioState = MIRCROELEC_BIO_IDLE;
            continue;
        }
        if (ptThis->tBioCommand.eCommand != MIRCROELEC_COMMAND_START) {
            continue;
        }
        if ((ptThis->eState != MIRCROELEC_STATE_READY)
            || (ptThis->tDac.eState != DAC_DRIVER_STATE_IDLE)) {
            ptThis->eBioState = MIRCROELEC_BIO_ERROR;
            continue;
        }

        ptThis->ptBioWave = ptThis->tBioCommand.ptWave;
        if (ptThis->ptBioWave == NULL) {
            ptThis->eBioState = MIRCROELEC_BIO_ERROR;
            continue;
        }
        MDI_IO_Write(bio_power, 1U);
        MDI_IO_Write(fan, 1U);
        ptThis->lBioDeadline = 0;
        ptThis->eBioState = MIRCROELEC_BIO_WAITING;

        while (ptThis->eBioState == MIRCROELEC_BIO_WAITING) {
            if (_mircoelec_ReceiveBioCommand(ptThis)) {
                if (ptThis->tBioCommand.eCommand == MIRCROELEC_COMMAND_STOP) {
                    MDI_IO_Write(bio_power, 0U);
                    MDI_IO_Write(fan, 0U);
                    ptThis->eBioState = MIRCROELEC_BIO_IDLE;
                }
            }
            if ((ptThis->eBioState == MIRCROELEC_BIO_WAITING)
                && perfc_is_time_out_ms(500U, &ptThis->lBioDeadline, false)) {
                eResult = mircoelec_StartConfiguredWaveform(ptThis,
                    ptThis->ptBioWave->eWaveType,
                    ptThis->ptBioWave->wAHz,
                    ptThis->ptBioWave->wBHz,
                    ptThis->ptBioWave->chAmplitudePercent);
                if (eResult == DAC_DRIVER_RESULT_OK) {
                    ptThis->eBioState = MIRCROELEC_BIO_RUNNING;
                } else {
                    (void)mircoelec_StopWaveform(ptThis);
                    MDI_IO_Write(bio_power, 0U);
                    MDI_IO_Write(fan, 0U);
                    ptThis->eBioState = MIRCROELEC_BIO_ERROR;
                }
                if (eResult != DAC_DRIVER_RESULT_OK) {
                    PERFC_PT_YIELD(fsm_rt_err)
                }
            }
            PERFC_PT_YIELD(fsm_rt_on_going)
        }

        while ((ptThis->eBioState == MIRCROELEC_BIO_RUNNING)
               || (ptThis->eBioState == MIRCROELEC_BIO_ERROR)) {
            PERFC_PT_WAIT_UNTIL(_mircoelec_ReceiveBioCommand(ptThis))
            if (ptThis->tBioCommand.eCommand == MIRCROELEC_COMMAND_STOP) {
                eResult = DAC_DRIVER_RESULT_OK;
                if (ptThis->eBioState == MIRCROELEC_BIO_RUNNING) {
                    eResult = mircoelec_StopWaveform(ptThis);
                }
                MDI_IO_Write(bio_power, 0U);
                MDI_IO_Write(fan, 0U);
                ptThis->eBioState = (eResult == DAC_DRIVER_RESULT_OK)
                    ? MIRCROELEC_BIO_IDLE : MIRCROELEC_BIO_ERROR;
            }
        }
    }
    PERFC_PT_END()
    return fsm_rt_cpl;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

/**
 * @brief Validate the Class required foreground callback.
 * @param wObjectAddr Address of the MODUS Class instance.
 * @return MODUS_SUCCESS on success; otherwise a MODUS error code.
 */
int mircoelec_Run(uintptr_t wObjectAddr)
{
    mircoelec_t *ptThis = (mircoelec_t *)wObjectAddr;
    fsm_rt_t eBioResult = fsm_rt_err;

    if ((ptThis == NULL) || (ptThis->eState == MIRCROELEC_STATE_UNINITIALIZED)) {
        return MODUS_EFAIL;
    }
    if (ptThis->eState == MIRCROELEC_STATE_READY) {
        heater_control_Run(&ptThis->tHeater);
    }
    eBioResult = _mircoelec_RunBio(ptThis);
    return (eBioResult == fsm_rt_err) ? MODUS_EFAIL : MODUS_SUCCESS;
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

    heater_control_Clock(&ptThis->tHeater);
    return MODUS_SUCCESS;
}
