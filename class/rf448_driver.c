/**
 * @file rf448_driver.c
 * @brief Non-blocking fixed-carrier and relay startup sequence.
 * @author Modus project
 * @date 2026-10-14
 */
#include "rf448_driver.h"
#include "mdi/instance.h"
#include "mdebug/util_debug.h"
#include "perfc_task_pt.h"
#include "rf448_ntc_lut.h"
#include <stddef.h>
#include <string.h>

#define RF448_HEAD_RELAY_MASK 0x38U
#define RF448_CAP_RELAYS 0x10U
#define RF448_RES_RELAYS 0x38U
#define RF448_NTC_ADC_MAX_CODE 4095U
#define RF448_BUS_ADC_REFERENCE_MV 3300U
#define RF448_BUS_ADC_FULL_SCALE 4095U
#define RF448_BUS_DIVIDER_RATIO 19U

/**
 * @brief Establish safe outputs even when one PWM provider reports failure.
 * @return OK when both PWM resources are safe, otherwise IO.
 */
static rf448_result_t _rf448_driver_SafeOutput(void)
{
    mdi_status_t eCarrier = MDI_OK;
    mdi_status_t eEnergy = MDI_OK;
    /* Logical SD=1; the MDI binding drives PB1 low through Q5 inversion. */
    MDI_IO_Write(pt_sd, 1U);
    eCarrier = MDI_PWM_SafeStop(pwm1);
    /* The external NPN inverts DACPWM: safe energy requires PA6 high. */
    eEnergy = MDI_PWM_SafeStop(dac_pwm);
    MDI_IO_WriteMasked(pt_relay, RF448_HEAD_RELAY_MASK, 0U);
    return ((eCarrier == MDI_OK) && (eEnergy == MDI_OK))
         ? RF448_RESULT_OK : RF448_RESULT_IO;
}

/**
 * @brief Latch an error and make a best effort to inhibit all RF output.
 * @param ptThis Driver receiving the failure.
 * @param eError Original operation failure.
 * @return None; the caller returns the failure to its own caller.
 */
static void _rf448_driver_Fail(rf448_driver_t *ptThis,
                               rf448_result_t eError)
{
    rf448_result_t eSafe = _rf448_driver_SafeOutput();
    ptThis->eLastError = eSafe == RF448_RESULT_OK ? eError : eSafe;
    ptThis->hwAppliedEnergyPermille = 0U;
    ptThis->eState = RF448_STATE_ERROR;
    ptThis->eRequest = RF448_REQUEST_NONE;
    ptThis->chRunPt = 0U;
    MLOGF(E, "RF448 error=%u safe=%u\r\n",
          (unsigned)eError, (unsigned)eSafe);
}

/**
 * @brief Apply the requested normalized level through the inverted PWM path.
 * @param hwEnergyPermille Valid target in 0..1000.
 * @return OK or IO when staging or committing fails.
 */
static rf448_result_t _rf448_driver_ApplyEnergy(uint16_t hwEnergyPermille)
{
    const MDI_PWM_Frame(dac_pwm) tFrame = {
        .duty = AT32_CLIENT_DAC_PWM_PERIOD_TICKS
            - ((uint32_t)hwEnergyPermille * AT32_CLIENT_DAC_PWM_PERIOD_TICKS
               / RF448_ENERGY_FULL_SCALE),
    };
    mdi_status_t eStatus = MDI_PWM_Stage(dac_pwm, &tFrame);
    if (eStatus == MDI_OK) { eStatus = MDI_PWM_Commit(dac_pwm); }
    return eStatus == MDI_OK ? RF448_RESULT_OK : RF448_RESULT_IO;
}

/** @copydoc rf448_driver_Init */
rf448_result_t rf448_driver_Init(rf448_driver_t *ptThis,
                                const rf448_driver_cfg_t *ptCfg)
{
    rf448_result_t eResult = RF448_RESULT_OK;
    if (ptThis == NULL) { return RF448_RESULT_INVALID; }
    (void)memset(ptThis, 0, sizeof(*ptThis));
    eResult = _rf448_driver_SafeOutput();
    if (eResult != RF448_RESULT_OK) {
        _rf448_driver_Fail(ptThis, eResult);
        return eResult;
    }
    if ((ptCfg == NULL) || (ptCfg->wRelaySettleMs == 0U)
        || (ptCfg->wRelaySettleMs > RF448_SETTLE_MAX_MS)
        || (ptCfg->wControlSettleMs == 0U)
        || (ptCfg->wControlSettleMs > RF448_SETTLE_MAX_MS)) {
        ptThis->eState = RF448_STATE_ERROR;
        ptThis->eLastError = RF448_RESULT_INVALID;
        MLOGF(E, "RF448 invalid startup delays\r\n");
        return RF448_RESULT_INVALID;
    }
    ptThis->wRelaySettleMs = ptCfg->wRelaySettleMs;
    ptThis->wControlSettleMs = ptCfg->wControlSettleMs;
    ptThis->eState = RF448_STATE_IDLE;
    return RF448_RESULT_OK;
}

/** @copydoc rf448_driver_Start */
rf448_result_t rf448_driver_Start(rf448_driver_t *ptThis,
    rf448_head_t eHead, uint16_t hwEnergyPermille, rf448_mode_t eMode)
{
    if ((ptThis == NULL)
        || ((eHead != RF448_HEAD_CAP) && (eHead != RF448_HEAD_RES))
        || (hwEnergyPermille > RF448_ENERGY_FULL_SCALE)
        || ((eMode != RF448_MODE_OPEN_LOOP)
            && (eMode != RF448_MODE_CLOSED_LOOP))) {
        return RF448_RESULT_INVALID;
    }
    if (ptThis->eState == RF448_STATE_UNINITIALIZED) {
        return RF448_RESULT_NOT_READY;
    }
    if (ptThis->eState == RF448_STATE_ERROR) { return RF448_RESULT_FAULT; }
    if (ptThis->eRequest != RF448_REQUEST_NONE) {
        return RF448_RESULT_BUSY;
    }
    if ((ptThis->eState != RF448_STATE_IDLE)
        && ((ptThis->eState != RF448_STATE_RUNNING)
            || (eHead != ptThis->eTargetHead)
            || (eMode != ptThis->eMode))) {
        return RF448_RESULT_BUSY;
    }
    if (ptThis->eState == RF448_STATE_IDLE) {
        ptThis->wInputFlags = 0U;
        ptThis->chContactSamples = 0U;
        ptThis->chContactHits = 0U;
        ptThis->bRf0iValid = false;
        ptThis->lBusSettleDeadline = 0;
        ptThis->lBusFaultDeadline = 0;
        ptThis->lContactDeadline = 0;
        ptThis->lFeedbackDeadline = 0;
    }
    ptThis->eMode = eMode;
    ptThis->eTargetHead = eHead;
    ptThis->hwTargetEnergyPermille = hwEnergyPermille;
    ptThis->eRequest = ptThis->eState == RF448_STATE_RUNNING
                    ? RF448_REQUEST_ENERGY : RF448_REQUEST_START;
    return RF448_RESULT_OK;
}

/** @brief Classify contact over non-overlapping sample windows. */
static void _rf448_driver_UpdateContact(rf448_driver_t *ptThis)
{
    if (!perfc_is_time_out_ms(RF448_CONTACT_PERIOD_MS,
                             &ptThis->lContactDeadline, true)) { return; }
    mdi_adc_value_t tValue = {0};
    ptThis->bRf0iValid = MDI_ADC_Read(client_rfi, &tValue) == MDI_OK;
    if (!ptThis->bRf0iValid) {
        ptThis->wInputFlags &= ~(RF448_INPUT_CONTACT_VALID
                                | RF448_INPUT_CONTACT_PRESENT);
        ptThis->chContactSamples = 0U;
        ptThis->chContactHits = 0U;
        return;
    }
    ptThis->hwRf0iAdc = (uint16_t)tValue.wCode;
    if (ptThis->eMode != RF448_MODE_CLOSED_LOOP) { return; }
    ptThis->chContactSamples++;
    if (tValue.wCode >= RF448_CONTACT_ADC_THRESHOLD) {
        ptThis->chContactHits++;
    }
    if (ptThis->chContactSamples >= RF448_CONTACT_WINDOW_SAMPLES) {
        ptThis->wInputFlags |= RF448_INPUT_CONTACT_VALID;
        if (ptThis->chContactHits >= RF448_CONTACT_CONFIRM_SAMPLES) {
            ptThis->wInputFlags |= RF448_INPUT_CONTACT_PRESENT;
        } else if ((RF448_CONTACT_WINDOW_SAMPLES - ptThis->chContactHits)
                   >= RF448_CONTACT_CONFIRM_SAMPLES) {
            ptThis->wInputFlags &= ~RF448_INPUT_CONTACT_PRESENT;
        } else {
            /* A tied window retains the previous classification. */
        }
        ptThis->chContactSamples = 0U;
        ptThis->chContactHits = 0U;
    }
}

/** @brief Resolve actual output without changing the requested target. */
static uint16_t _rf448_driver_SelectEnergy(const rf448_driver_t *ptThis)
{
    if ((ptThis->eMode == RF448_MODE_CLOSED_LOOP)
        && ((ptThis->wInputFlags & (RF448_INPUT_CONTACT_VALID
                                  | RF448_INPUT_CONTACT_PRESENT))
            != (RF448_INPUT_CONTACT_VALID | RF448_INPUT_CONTACT_PRESENT))
        && (ptThis->hwTargetEnergyPermille > RF448_LOW_ENERGY_PERMILLE)) {
        return RF448_LOW_ENERGY_PERMILLE;
    }
    return ptThis->hwTargetEnergyPermille;
}

/** @brief Commit output changes and retain the last successful level. */
static rf448_result_t _rf448_driver_UpdateEnergy(rf448_driver_t *ptThis)
{
    uint16_t hwEnergy = _rf448_driver_SelectEnergy(ptThis);
    rf448_result_t eResult = RF448_RESULT_OK;
    if (hwEnergy != ptThis->hwAppliedEnergyPermille) {
        eResult = _rf448_driver_ApplyEnergy(hwEnergy);
        if (eResult == RF448_RESULT_OK) {
            ptThis->hwAppliedEnergyPermille = hwEnergy;
            ptThis->lBusSettleDeadline = 0;
            ptThis->lBusFaultDeadline = 0;
            ptThis->wInputFlags &= ~(RF448_INPUT_BUS_CHECKED
                                    | RF448_INPUT_BUS_MISMATCH);
        }
    }
    return eResult;
}

/**
 * @brief Select the requested head while RF and energy remain inhibited.
 * @param ptThis Driver holding an accepted head request.
 * @return OK or IO; the sequencer latches failures.
 */
static rf448_result_t _rf448_driver_BeginHead(rf448_driver_t *ptThis)
{
    rf448_result_t eResult = _rf448_driver_SafeOutput();
    if (eResult != RF448_RESULT_OK) { return eResult; }
    MDI_IO_WriteMasked(pt_relay, RF448_HEAD_RELAY_MASK,
        ptThis->eTargetHead == RF448_HEAD_CAP
        ? RF448_CAP_RELAYS : RF448_RES_RELAYS);
    ptThis->hwAppliedEnergyPermille = 0U;
    ptThis->eState = RF448_STATE_RELAY_WAIT;
    MLOGF(I, "RF448 state: RELAY_WAIT head=%u\r\n",
          (unsigned)ptThis->eTargetHead);
    return RF448_RESULT_OK;
}

/**
 * @brief Enable the prepared fixed carrier and release the external shutdown.
 * @param ptThis Driver whose relay and energy waits have completed.
 * @return OK or IO; failed enable leaves SD asserted.
 */
static rf448_result_t _rf448_driver_EnableCarrier(rf448_driver_t *ptThis)
{
    mdi_status_t eStatus = MDI_PWM_Enable(pwm1, true);
    if (eStatus != MDI_OK) { return RF448_RESULT_IO; }
    /* Logical SD=0 drives PB1 high, turning Q5 on and releasing shutdown. */
    MDI_IO_Write(pt_sd, 0U);
    ptThis->eState = RF448_STATE_RUNNING;
    MLOGF(I, "RF448 state: RUNNING energy=%u/1000\r\n",
          (unsigned)ptThis->hwAppliedEnergyPermille);
    return RF448_RESULT_OK;
}

/** @brief Publish both MOS temperatures; only Run handles protection. */
static void _rf448_driver_UpdateMosTemperature(rf448_driver_t *ptThis)
{
    mdi_adc_value_t tValue = {0};
    ptThis->wInputFlags &= ~(RF448_INPUT_NTC12_VALID
        | RF448_INPUT_NTC12_OVER_TEMP | RF448_INPUT_NTC11_VALID
        | RF448_INPUT_NTC11_OVER_TEMP);
    if ((MDI_ADC_Read(client_ntc12, &tValue) == MDI_OK)
        && rf448_driver_ConvertNtcAdcToDeciCelsius(
            (uint16_t)tValue.wCode, &ptThis->nNtc12DeciC)) {
        ptThis->wInputFlags |= RF448_INPUT_NTC12_VALID;
        if (ptThis->nNtc12DeciC > RF448_OVERTEMP_THRESHOLD_DECI_C) {
            ptThis->wInputFlags |= RF448_INPUT_NTC12_OVER_TEMP;
        }
    }
    if ((MDI_ADC_Read(client_ntc11, &tValue) == MDI_OK)
        && rf448_driver_ConvertNtcAdcToDeciCelsius(
            (uint16_t)tValue.wCode, &ptThis->nNtc11DeciC)) {
        ptThis->wInputFlags |= RF448_INPUT_NTC11_VALID;
        if (ptThis->nNtc11DeciC > RF448_OVERTEMP_THRESHOLD_DECI_C) {
            ptThis->wInputFlags |= RF448_INPUT_NTC11_OVER_TEMP;
        }
    }
}

/** @brief Publish bus conversion and reserved match-check flags. */
static void _rf448_driver_UpdateBusVoltage(rf448_driver_t *ptThis)
{
    mdi_adc_value_t tValue = {0};
    ptThis->wInputFlags &= ~(RF448_INPUT_BUS_VALID
        | RF448_INPUT_BUS_CHECKED | RF448_INPUT_BUS_MISMATCH);

    ptThis->tBusVoltage.hwAdcCode = 0U;
    ptThis->tBusVoltage.wMilliVolt = 0U;
    ptThis->tBusVoltage.bValid = false;
    if (MDI_ADC_Read(client_ivadc, &tValue) == MDI_OK) {
        ptThis->tBusVoltage.hwAdcCode = (uint16_t)tValue.wCode;
        if ((tValue.wCode > 0U)
            && (tValue.wCode < RF448_BUS_ADC_FULL_SCALE)) {
            ptThis->tBusVoltage.wMilliVolt =
                ((uint32_t)tValue.wCode * RF448_BUS_ADC_REFERENCE_MV
                 * RF448_BUS_DIVIDER_RATIO
                 + (RF448_BUS_ADC_FULL_SCALE / 2U))
                / RF448_BUS_ADC_FULL_SCALE;
            ptThis->tBusVoltage.bValid = true;
            ptThis->wInputFlags |= RF448_INPUT_BUS_VALID;
        }
    }
#if RF448_BUS_CHECK_ENABLE
    if ((ptThis->eMode != RF448_MODE_CLOSED_LOOP)
        || (ptThis->eState != RF448_STATE_RUNNING)
        || (ptThis->eRequest == RF448_REQUEST_STOP)) {
        ptThis->lBusSettleDeadline = 0;
        ptThis->lBusFaultDeadline = 0;
        return;
    }
    if (!perfc_is_time_out_ms(RF448_BUS_SETTLE_MS,
                              &ptThis->lBusSettleDeadline, false)) {
        return;
    }
    uint32_t wTargetMv = (uint32_t)ptThis->hwAppliedEnergyPermille
        * RF448_BUS_TARGET_FULL_SCALE_MV / RF448_ENERGY_FULL_SCALE;
    uint32_t wActualMv = ptThis->tBusVoltage.wMilliVolt;
    uint32_t wDifference = wActualMv > wTargetMv
                        ? wActualMv - wTargetMv : wTargetMv - wActualMv;
    ptThis->wInputFlags |= RF448_INPUT_BUS_CHECKED;
    if (!ptThis->tBusVoltage.bValid
        || (wDifference > RF448_BUS_TOLERANCE_MV)) {
        if (perfc_is_time_out_ms(RF448_BUS_FAULT_MS,
                                 &ptThis->lBusFaultDeadline, false)) {
            ptThis->wInputFlags |= RF448_INPUT_BUS_MISMATCH;
        }
    } else {
        ptThis->lBusFaultDeadline = 0;
    }
#endif
}

/** @copydoc rf448_driver_Run */
fsm_rt_t rf448_driver_Run(rf448_driver_t *ptThis)
{
    rf448_result_t eResult = RF448_RESULT_OK;
    if ((ptThis == NULL)
        || (ptThis->eState == RF448_STATE_UNINITIALIZED)
        || (ptThis->eState == RF448_STATE_ERROR)) {
        return fsm_rt_err;
    }
    /* Detectors run independently of PT suspension; they never drive output. */
    if ((ptThis->eState == RF448_STATE_RELAY_WAIT)
        || (ptThis->eState == RF448_STATE_ENERGY_WAIT)
        || (ptThis->eState == RF448_STATE_RUNNING)) {
        if ((ptThis->eState == RF448_STATE_RUNNING)
            && (ptThis->eRequest != RF448_REQUEST_STOP)) {
            _rf448_driver_UpdateContact(ptThis);
        }
        if (perfc_is_time_out_ms(RF448_FEEDBACK_PERIOD_MS,
                &ptThis->lFeedbackDeadline, true)) {
            _rf448_driver_UpdateMosTemperature(ptThis);
            _rf448_driver_UpdateBusVoltage(ptThis);
        }
        if ((ptThis->wInputFlags & RF448_INPUT_FAULT_MASK) != 0U) {
            MLOGF(E, "RF448 input fault flags=0x%lx; stopping\r\n",
                  (unsigned long)ptThis->wInputFlags);
            _rf448_driver_Fail(ptThis, RF448_RESULT_FAULT);
            return fsm_rt_err;
        }
    }
    PERFC_PT_BEGIN(ptThis->chRunPt)
    while (1) {
        while (ptThis->eRequest == RF448_REQUEST_NONE) {
            PERFC_PT_YIELD(fsm_rt_cpl)
        }
        if (ptThis->eRequest == RF448_REQUEST_START) {
            ptThis->eRequest = RF448_REQUEST_NONE;
            do {
                eResult = _rf448_driver_BeginHead(ptThis);
                if (eResult != RF448_RESULT_OK) {
                    _rf448_driver_Fail(ptThis, eResult);
                    return fsm_rt_err;
                }
                ptThis->lWaitDeadline = 0;
                PERFC_PT_WAIT_UNTIL(
                    (ptThis->eRequest == RF448_REQUEST_STOP)
                    || perfc_is_time_out_ms(ptThis->wRelaySettleMs,
                                            &ptThis->lWaitDeadline, false))
                if (ptThis->eRequest == RF448_REQUEST_STOP) { break; }
                eResult = _rf448_driver_UpdateEnergy(ptThis);
                if (eResult != RF448_RESULT_OK) {
                    _rf448_driver_Fail(ptThis, eResult);
                    return fsm_rt_err;
                }
                ptThis->eState = RF448_STATE_ENERGY_WAIT;
                MLOGF(I, "RF448 state: ENERGY_WAIT\r\n");
                ptThis->lWaitDeadline = 0;
                PERFC_PT_WAIT_UNTIL(
                    (ptThis->eRequest == RF448_REQUEST_STOP)
                    || perfc_is_time_out_ms(ptThis->wControlSettleMs,
                                            &ptThis->lWaitDeadline, false))
                if (ptThis->eRequest == RF448_REQUEST_STOP) { break; }
                eResult = _rf448_driver_EnableCarrier(ptThis);
                if (eResult != RF448_RESULT_OK) {
                    _rf448_driver_Fail(ptThis, eResult);
                    return fsm_rt_err;
                }
                while (ptThis->eRequest != RF448_REQUEST_STOP) {
                    eResult = _rf448_driver_UpdateEnergy(ptThis);
                    if (eResult != RF448_RESULT_OK) {
                        _rf448_driver_Fail(ptThis, eResult);
                        return fsm_rt_err;
                    }
                    ptThis->eRequest = RF448_REQUEST_NONE;
                    PERFC_PT_YIELD(fsm_rt_cpl)
                }
            } while (0);
        }
        /* Stop already made hardware safe; finish its lifecycle inside PT. */
        ptThis->eRequest = RF448_REQUEST_NONE;
        ptThis->hwTargetEnergyPermille = 0U;
        ptThis->hwAppliedEnergyPermille = 0U;
        ptThis->wInputFlags &= ~(RF448_INPUT_CONTACT_VALID
                                | RF448_INPUT_CONTACT_PRESENT);
        ptThis->lWaitDeadline = 0;
        ptThis->eState = RF448_STATE_IDLE;
        MLOGF(I, "RF448 state: IDLE\r\n");
        PERFC_PT_YIELD(fsm_rt_cpl)
    }
    PERFC_PT_END()
    return fsm_rt_cpl;
}

/** @copydoc rf448_driver_Stop */
rf448_result_t rf448_driver_Stop(rf448_driver_t *ptThis)
{
    rf448_result_t eResult = RF448_RESULT_OK;
    if (ptThis == NULL) { return RF448_RESULT_INVALID; }
    eResult = _rf448_driver_SafeOutput();
    if (eResult != RF448_RESULT_OK) {
        ptThis->eLastError = eResult;
        ptThis->eState = RF448_STATE_ERROR;
        MLOGF(E, "RF448 stop failed=%u\r\n", (unsigned)eResult);
        return eResult;
    }
    ptThis->hwAppliedEnergyPermille = 0U;
    ptThis->wInputFlags &= ~(RF448_INPUT_CONTACT_VALID
                                | RF448_INPUT_CONTACT_PRESENT);
    ptThis->eRequest = RF448_REQUEST_STOP;
    return RF448_RESULT_OK;
}

/** @copydoc rf448_driver_GetStatus */
rf448_result_t rf448_driver_GetStatus(const rf448_driver_t *ptThis,
                                     rf448_driver_status_t *ptStatus)
{
    if ((ptThis == NULL) || (ptStatus == NULL)) { return RF448_RESULT_INVALID; }
    ptStatus->eState = ptThis->eState;
    ptStatus->eLastError = ptThis->eLastError;
    ptStatus->eTargetHead = ptThis->eTargetHead;
    ptStatus->hwTargetEnergyPermille = ptThis->hwTargetEnergyPermille;
    ptStatus->hwAppliedEnergyPermille = ptThis->hwAppliedEnergyPermille;
    ptStatus->eMode = ptThis->eMode;
    ptStatus->wInputFlags = ptThis->wInputFlags;
    ptStatus->bOutputEnabled = MDI_PWM_IsEnabled(pwm1);
    return RF448_RESULT_OK;
}

/** @copydoc rf448_driver_GetBusVoltage */
rf448_result_t rf448_driver_GetBusVoltage(
    const rf448_driver_t *ptThis,
    rf448_driver_bus_voltage_t *ptVoltage)
{
    if ((ptThis == NULL) || (ptVoltage == NULL)) {
        return RF448_RESULT_INVALID;
    }
    *ptVoltage = ptThis->tBusVoltage;
    return RF448_RESULT_OK;
}

/** @copydoc rf448_driver_ConvertNtcAdcToDeciCelsius */
bool rf448_driver_ConvertNtcAdcToDeciCelsius(uint16_t wAdcCode,
                                           int16_t *pnTemperatureDeciC)
{
    if ((pnTemperatureDeciC == NULL) || (wAdcCode == 0U)
        || (wAdcCode >= RF448_NTC_ADC_MAX_CODE)) {
        return false;
    }
    *pnTemperatureDeciC = s_nRf448NtcTemperatureDeciC[wAdcCode];
    return true;
}
