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
    rf448_head_t eHead, uint16_t hwEnergyPermille)
{
    if ((ptThis == NULL)
        || ((eHead != RF448_HEAD_CAP) && (eHead != RF448_HEAD_RES))
        || (hwEnergyPermille > RF448_ENERGY_FULL_SCALE)) {
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
            || (eHead != ptThis->eTargetHead))) {
        return RF448_RESULT_BUSY;
    }
    ptThis->eTargetHead = eHead;
    ptThis->hwTargetEnergyPermille = hwEnergyPermille;
    ptThis->eRequest = ptThis->eState == RF448_STATE_RUNNING
                    ? RF448_REQUEST_ENERGY : RF448_REQUEST_START;
    return RF448_RESULT_OK;
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
          (unsigned)ptThis->hwTargetEnergyPermille);
    return RF448_RESULT_OK;
}

static uint8_t _rf448_driver_ReadOverTemperature(int16_t *pnTemperatureDeciC)
{
    mdi_adc_value_t tValue = {0};
    int16_t nTemperatureDeciC = 0;
    if (pnTemperatureDeciC == NULL) { return 0U; }
    if ((MDI_ADC_Read(client_ntc12, &tValue) == MDI_OK)
        && rf448_driver_ConvertNtcAdcToDeciCelsius(
            (uint16_t)tValue.wCode, &nTemperatureDeciC)
        && (nTemperatureDeciC > RF448_OVERTEMP_THRESHOLD_DECI_C)) {
        *pnTemperatureDeciC = nTemperatureDeciC;
        return 12U;
    }
    if ((MDI_ADC_Read(client_ntc11, &tValue) == MDI_OK)
        && rf448_driver_ConvertNtcAdcToDeciCelsius(
            (uint16_t)tValue.wCode, &nTemperatureDeciC)
        && (nTemperatureDeciC > RF448_OVERTEMP_THRESHOLD_DECI_C)) {
        *pnTemperatureDeciC = nTemperatureDeciC;
        return 11U;
    }
    return 0U;
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
                eResult = _rf448_driver_ApplyEnergy(
                    ptThis->hwTargetEnergyPermille);
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
                    int16_t nTemperatureDeciC = 0;
                    uint8_t chNtc = _rf448_driver_ReadOverTemperature(
                        &nTemperatureDeciC);
                    if (chNtc != 0U) {
                        MLOGF(E, "RF448 over-temperature NTC%u=%d.%dC; stopping\r\n",
                              (unsigned)chNtc,
                              (int)(nTemperatureDeciC / 10),
                              (int)(nTemperatureDeciC % 10));
                        eResult = rf448_driver_Stop(ptThis);
                        if (eResult != RF448_RESULT_OK) { return fsm_rt_err; }
                        break;
                    }
                    if (ptThis->eRequest == RF448_REQUEST_ENERGY) {
                        eResult = _rf448_driver_ApplyEnergy(
                            ptThis->hwTargetEnergyPermille);
                        if (eResult != RF448_RESULT_OK) {
                            _rf448_driver_Fail(ptThis, eResult);
                            return fsm_rt_err;
                        }
                        ptThis->eRequest = RF448_REQUEST_NONE;
                        MLOGF(I, "RF448 energy updated=%u/1000\r\n",
                            (unsigned)ptThis->hwTargetEnergyPermille);
                    }
                    PERFC_PT_YIELD(fsm_rt_cpl)
                }
            } while (0);
        }
        /* Stop already made hardware safe; finish its lifecycle inside PT. */
        ptThis->eRequest = RF448_REQUEST_NONE;
        ptThis->hwTargetEnergyPermille = 0U;
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
    ptStatus->bOutputEnabled = MDI_PWM_IsEnabled(pwm1);
    return RF448_RESULT_OK;
}

/** @copydoc rf448_driver_ConvertNtcAdcToDeciCelsius */
bool rf448_driver_ConvertNtcAdcToDeciCelsius(uint16_t wAdcCode,
                                           int16_t *pnTemperatureDeciC)
{
    if ((pnTemperatureDeciC == NULL) || (wAdcCode == 0U)) {
        return false;
    }
    *pnTemperatureDeciC = s_nRf448NtcTemperatureDeciC[wAdcCode];
    return true;
}
