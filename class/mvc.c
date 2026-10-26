/**
 * @file mvc.c
 * @brief RF448 class assembly and MShell control.
 * @author Modus project
 * @date 2026-10-14
 */
#include "mvc.h"
#include "mdi/instance.h"
#include "mdebug/mshell.h"
#include "mdebug/util_debug.h"
#include "perf_counter.h"
#include <stddef.h>
#include <string.h>

#define MVC_RELAY_SETTLE_MS 30U
#define MVC_CONTROL_SETTLE_MS 1U

/**
 * @brief Enable or disable both cooling fans when the state changes.
 * @param ptThis MVC instance holding the last commanded fan state.
 * @param bEnable True enables FAN2 and FAN3; false disables both.
 * @return None.
 */
static void _mvc_SetFans(mvc_t *ptThis, bool bEnable)
{
    if (ptThis->bFansEnabled != bEnable) {
        MDI_IO_Write(pt_fan, bEnable ? 0x03U : 0U);
        ptThis->bFansEnabled = bEnable;
    }
}

/**
 * @brief Log all latest averaged ADC codes once per second.
 * @param ptThis MVC instance holding the log deadline.
 * @return None; read status is printed with each value.
 */
static void _mvc_LogAdc(mvc_t *ptThis)
{
    mdi_adc_value_t tIvadc = {0};
    mdi_adc_value_t tNtc12 = {0};
    mdi_adc_value_t tNtc11 = {0};
    mdi_adc_value_t tRfv = {0};
    mdi_adc_value_t tRfi = {0};
    mdi_status_t eIvadc;
    mdi_status_t eNtc12;
    mdi_status_t eNtc11;
    mdi_status_t eRfv;
    mdi_status_t eRfi;
    int16_t nNtc12TemperatureDeciC = 0;
    int16_t nNtc11TemperatureDeciC = 0;
    int32_t nNtc12Magnitude = 0;
    int32_t nNtc11Magnitude = 0;
    bool bNtc12TemperatureValid = false;
    bool bNtc11TemperatureValid = false;
    if (!perfc_is_time_out_ms(1000U, &ptThis->lAdcLogDeadline, true)) {
        return;
    }
    eIvadc = MDI_ADC_Read(client_ivadc, &tIvadc);
    eNtc12 = MDI_ADC_Read(client_ntc12, &tNtc12);
    eNtc11 = MDI_ADC_Read(client_ntc11, &tNtc11);
    eRfv = MDI_ADC_Read(client_rfv, &tRfv);
    eRfi = MDI_ADC_Read(client_rfi, &tRfi);
    bNtc12TemperatureValid = (eNtc12 == MDI_OK)
        && rf448_driver_ConvertNtcAdcToDeciCelsius(
            tNtc12.wCode, &nNtc12TemperatureDeciC);
    bNtc11TemperatureValid = (eNtc11 == MDI_OK)
        && rf448_driver_ConvertNtcAdcToDeciCelsius(
            tNtc11.wCode, &nNtc11TemperatureDeciC);
    nNtc12Magnitude = nNtc12TemperatureDeciC < 0
                    ? -(int32_t)nNtc12TemperatureDeciC
                    : (int32_t)nNtc12TemperatureDeciC;
    nNtc11Magnitude = nNtc11TemperatureDeciC < 0
                    ? -(int32_t)nNtc11TemperatureDeciC
                    : (int32_t)nNtc11TemperatureDeciC;
    MLOGF(T, "ADC IVADC=%u(%d) NTC12=%u(%d) NTC11=%u(%d) "
             "RF0V=%u(%d) RF0I=%u(%d)\r\n"
             "NTC12 temperature=%s%d.%dC(%s) "
             "NTC11 temperature=%s%d.%dC(%s)\r\n",
          (unsigned)tIvadc.wCode, (int)eIvadc,
          (unsigned)tNtc12.wCode, (int)eNtc12,
          (unsigned)tNtc11.wCode, (int)eNtc11,
          (unsigned)tRfv.wCode, (int)eRfv,
          (unsigned)tRfi.wCode, (int)eRfi,
          nNtc12TemperatureDeciC < 0 ? "-" : "",
          (int)(nNtc12Magnitude / 10),
          (int)(nNtc12Magnitude % 10),
          bNtc12TemperatureValid ? "valid" : "invalid",
          nNtc11TemperatureDeciC < 0 ? "-" : "",
          (int)(nNtc11Magnitude / 10),
          (int)(nNtc11Magnitude % 10),
          bNtc11TemperatureValid ? "valid" : "invalid");
}

/** @copydoc mvc_Init */
int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    const mvc_cfg_t *ptCfg = (const mvc_cfg_t *)wObjectCfgAddr;
    rf448_result_t eResult = RF448_RESULT_OK;
    int nResult = MODUS_EFAIL;
    if (ptThis == NULL) { return MODUS_EFAIL; }
    MDI_IO_Write(pt_fan, 0U);
    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = MVC_STATE_UNINITIALIZED;
    if ((ptCfg == NULL) || (ptCfg->wObjectId == 0U)) {
        eResult = rf448_driver_Stop(&ptThis->tRf448);
        ptThis->eState = MVC_STATE_ERROR;
        MLOGF(E, "MVC invalid config; stop=%u\r\n", (unsigned)eResult);
        return MODUS_EFAIL;
    }
    eResult = rf448_driver_Init(&ptThis->tRf448, &ptCfg->tRf448Cfg);
    if (eResult != RF448_RESULT_OK) {
        (void)rf448_driver_Stop(&ptThis->tRf448);
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }
    ptThis->tBaseCfg.wId = ptCfg->wObjectId;
    ptThis->tBaseCfg.wParent = wObjectAddr;
    ptThis->tBaseCfg.FcnInterface.Clock = mvc_Clock;
    ptThis->tBaseCfg.FcnInterface.Run = mvc_Run;
    nResult = mbase_Init(&ptThis->tBase, &ptThis->tBaseCfg);
    if (nResult != MODUS_SUCCESS) {
        eResult = rf448_driver_Stop(&ptThis->tRf448);
        ptThis->eState = MVC_STATE_ERROR;
        MLOGF(E, "MVC registration failed=%d; stop=%u\r\n",
              nResult, (unsigned)eResult);
        return MODUS_EFAIL;
    }
    ptThis->eState = MVC_STATE_IDLE;
    return MODUS_SUCCESS;
}

/** @copydoc mvc_Clock */
int mvc_Clock(uintptr_t wObjectAddr)
{
    /* MODUS calls every registered Clock; RF448 has no periodic ISR work. */
    return wObjectAddr != 0U ? MODUS_SUCCESS : MODUS_EFAIL;
}

/** @copydoc mvc_Run */
int mvc_Run(uintptr_t wObjectAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    rf448_driver_status_t tStatus = {0};
    rf448_result_t eResult = RF448_RESULT_OK;
    fsm_rt_t eRun = fsm_rt_err;
    if (ptThis == NULL) { return MODUS_EFAIL; }
    if (ptThis->eState != MVC_STATE_UNINITIALIZED) {
        _mvc_LogAdc(ptThis);
    }
    if ((ptThis->eState == MVC_STATE_UNINITIALIZED)
        || (ptThis->eState == MVC_STATE_ERROR)) {
        return MODUS_EFAIL;
    }
    eRun = rf448_driver_Run(&ptThis->tRf448);
    eResult = rf448_driver_GetStatus(&ptThis->tRf448, &tStatus);
    if ((eRun == fsm_rt_err) || (eResult != RF448_RESULT_OK)) {
        _mvc_SetFans(ptThis, false);
        ptThis->eState = MVC_STATE_ERROR;
        MLOGF(E, "MVC RF448 failed=%u\r\n", (unsigned)tStatus.eLastError);
        return MODUS_EFAIL;
    }
    _mvc_SetFans(ptThis, tStatus.bOutputEnabled);
    ptThis->eState = tStatus.eState == RF448_STATE_IDLE
                  ? MVC_STATE_IDLE : MVC_STATE_RUNNING;
    return MODUS_SUCCESS;
}

MODUS_DECLARE_OBJECT(mvc, Mvc,
    .wObjectId = MVC,
    .tRf448Cfg = {
        .wRelaySettleMs = MVC_RELAY_SETTLE_MS,
        .wControlSettleMs = MVC_CONTROL_SETTLE_MS,
    }
)

#if MSHELL_ENABLE
/**
 * @brief Print the diagnostic snapshot through the selected Shell transport.
 * @return None; snapshot failures are reported through the debug logger.
 */
static void _mvc_PrintStatus(void)
{
    rf448_driver_status_t tStatus = {0};
    rf448_result_t eResult = rf448_driver_GetStatus(&tMvc.tRf448, &tStatus);
    if (eResult != RF448_RESULT_OK) {
        MLOGF(E, "RF448 status failed=%u\r\n", (unsigned)eResult);
        return;
    }
    MLOGF(I, "RF448 state=%u head=%s target=%u/1000 output=%u error=%u\r\n",
        (unsigned)tStatus.eState,
        tStatus.eTargetHead == RF448_HEAD_CAP ? "cap" : "res",
        (unsigned)tStatus.hwTargetEnergyPermille,
        (unsigned)tStatus.bOutputEnabled, (unsigned)tStatus.eLastError);
}

/**
 * @brief Parse the requested head and normalized level as one transaction.
 * @param pchArgs Arguments after the start token.
 * @return None; invalid input leaves the driver unchanged.
 */
static void _mvc_ShellStart(const char *pchArgs)
{
    rf448_head_t eHead = RF448_HEAD_CAP;
    uint16_t hwEnergy = 0U;
    rf448_result_t eResult = RF448_RESULT_INVALID;
    if (mshell_ConsumeWord(&pchArgs, "cap")) {
        eHead = RF448_HEAD_CAP;
    } else if (mshell_ConsumeWord(&pchArgs, "res")) {
        eHead = RF448_HEAD_RES;
    } else {
        mshell_Write("Usage: rf448 start cap|res <0..1000>\r\n");
        return;
    }
    if ((mshell_Parse(&pchArgs, &hwEnergy) != MSHELL_PARSE_OK)
        || !mshell_AtEnd(pchArgs) || (hwEnergy > RF448_ENERGY_FULL_SCALE)) {
        mshell_Write("Usage: rf448 start cap|res <0..1000>\r\n");
        return;
    }
    eResult = rf448_driver_Start(&tMvc.tRf448, eHead, hwEnergy);
    MLOGF(I, "RF448 start request result=%u\r\n", (unsigned)eResult);
}

/**
 * @brief Submit typed RF control operations without editing driver state.
 * @param pchArgs Complete text following the rf448 command.
 * @return None; operation failures are reported in the Shell log.
 */
static void _mvc_ShellRf448(const char *pchArgs)
{
    rf448_result_t eResult = RF448_RESULT_OK;
    if (mshell_ConsumeWord(&pchArgs, "start")) {
        _mvc_ShellStart(pchArgs);
    } else if (mshell_ConsumeWord(&pchArgs, "stop") && mshell_AtEnd(pchArgs)) {
        eResult = rf448_driver_Stop(&tMvc.tRf448);
        _mvc_SetFans(&tMvc, false);
        MLOGF(I, "RF448 stop result=%u\r\n", (unsigned)eResult);
    } else if (mshell_AtEnd(pchArgs)
        || (mshell_ConsumeWord(&pchArgs, "status") && mshell_AtEnd(pchArgs))) {
        _mvc_PrintStatus();
    } else {
        mshell_Write("Usage: rf448 start cap|res <0..1000> | stop | status\r\n");
    }
}
MODUS_SHELL_CMD(rf448, _mvc_ShellRf448,
    "RF448: start cap|res <0..1000> | stop | status");
#endif
