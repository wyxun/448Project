/**
 * @file    mvc_debug.c
 * @brief   Client MShell commands for manual hardware diagnostics.
 * @author  Modus project
 * @date    2026-10-29
 */
#include "mvc.h"
#include "mdi/instance.h"
#include "mdebug/mshell.h"
#include "mdebug/util_debug.h"

#if MSHELL_ENABLE
extern mvc_t tMvc;
/**
 * @brief Read and print all raw ADC channels once.
 * @param pchArgs Must contain only the read token.
 * @return None; each channel includes its MDI read status.
 */
static void _mvc_debug_AdcCommand(const char *pchArgs)
{
    mdi_adc_value_t tIvadc = {0};
    mdi_adc_value_t tNtc12 = {0};
    mdi_adc_value_t tNtc11 = {0};
    mdi_adc_value_t tRfv = {0};
    mdi_adc_value_t tRfi = {0};
    mdi_status_t eIvadc = MDI_OK;
    mdi_status_t eNtc12 = MDI_OK;
    mdi_status_t eNtc11 = MDI_OK;
    mdi_status_t eRfv = MDI_OK;
    mdi_status_t eRfi = MDI_OK;

    if (!mshell_ConsumeWord(&pchArgs, "read") || !mshell_AtEnd(pchArgs)) {
        mshell_Write("Usage: adc read\r\n");
        return;
    }
    eIvadc = MDI_ADC_Read(client_ivadc, &tIvadc);
    eNtc12 = MDI_ADC_Read(client_ntc12, &tNtc12);
    eNtc11 = MDI_ADC_Read(client_ntc11, &tNtc11);
    eRfv = MDI_ADC_Read(client_rfv, &tRfv);
    eRfi = MDI_ADC_Read(client_rfi, &tRfi);
    MLOGF(I, "ADC raw IVADC=%u(%d) NTC12=%u(%d) NTC11=%u(%d) "
             "RF0V=%u(%d) RF0I=%u(%d)\r\n",
          (unsigned)tIvadc.wCode, (int)eIvadc,
          (unsigned)tNtc12.wCode, (int)eNtc12,
          (unsigned)tNtc11.wCode, (int)eNtc11,
          (unsigned)tRfv.wCode, (int)eRfv,
          (unsigned)tRfi.wCode, (int)eRfi);
}

MODUS_SHELL_CMD(adc, _mvc_debug_AdcCommand, "Read raw ADC channels once");

/**
 * @brief Print the diagnostic snapshot through the selected Shell transport.
 * @return None; snapshot failures are reported through the debug logger.
 */
static void _mvc_PrintStatus(void)
{
    rf448_driver_status_t tStatus = {0};
    rf448_driver_bus_voltage_t tBus = {0};
    rf448_result_t eResult = rf448_driver_GetStatus(&tMvc.tRf448, &tStatus);
    if (eResult != RF448_RESULT_OK) {
        MLOGF(E, "RF448 status failed=%u\r\n", (unsigned)eResult);
        return;
    }
    MLOGF(I, "RF448 state=%u head=%s mode=%s output=%u error=%u\r\n",
        (unsigned)tStatus.eState,
        tStatus.eTargetHead == RF448_HEAD_CAP ? "cap" : "res",
        tStatus.eMode == RF448_MODE_CLOSED_LOOP ? "close" : "open",
        (unsigned)tStatus.bOutputEnabled, (unsigned)tStatus.eLastError);
    MLOGF(I, "RF448 target=%u applied=%u/1000 flags=0x%lx\r\n",
        (unsigned)tStatus.hwTargetEnergyPermille,
        (unsigned)tStatus.hwAppliedEnergyPermille,
        (unsigned long)tStatus.wInputFlags);
    eResult = rf448_driver_GetBusVoltage(&tMvc.tRf448, &tBus);
    if (eResult == RF448_RESULT_OK) {
        MLOGF(I, "RF448 bus=%lu mV adc=%u valid=%u\r\n",
            (unsigned long)tBus.wMilliVolt, (unsigned)tBus.hwAdcCode,
            (unsigned)tBus.bValid);
    }

}

/**
 * @brief Parse the requested head and normalized level as one transaction.
 * @param pchArgs Arguments after the start token.
 * @return None; invalid input leaves the driver unchanged.
 */
static void _mvc_ShellStart(const char *pchArgs)
{
    rf448_head_t eHead = RF448_HEAD_CAP;
    rf448_mode_t eMode = RF448_MODE_OPEN_LOOP;
    uint16_t hwEnergy = 0U;
    rf448_result_t eResult = RF448_RESULT_INVALID;
    if (mshell_ConsumeWord(&pchArgs, "cap")) {
        eHead = RF448_HEAD_CAP;
    } else if (mshell_ConsumeWord(&pchArgs, "res")) {
        eHead = RF448_HEAD_RES;
    } else {
        mshell_Write("Usage: rf448 start cap|res open|close <0..1000>\r\n");
        return;
    }
    if (mshell_ConsumeWord(&pchArgs, "open")) {
        eMode = RF448_MODE_OPEN_LOOP;
    } else if (mshell_ConsumeWord(&pchArgs, "close")) {
        eMode = RF448_MODE_CLOSED_LOOP;
    } else {
        mshell_Write("Usage: rf448 start cap|res open|close <0..1000>\r\n");
        return;
    }
    if ((mshell_Parse(&pchArgs, &hwEnergy) != MSHELL_PARSE_OK)
        || !mshell_AtEnd(pchArgs) || (hwEnergy > RF448_ENERGY_FULL_SCALE)) {
        mshell_Write("Usage: rf448 start cap|res open|close <0..1000>\r\n");
        return;
    }
    eResult = rf448_driver_Start(&tMvc.tRf448, eHead, hwEnergy,
                                  eMode);
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
        eResult = mvc_StopRf448(&tMvc);
        MLOGF(I, "RF448 stop result=%u\r\n", (unsigned)eResult);
    } else if (mshell_AtEnd(pchArgs)
        || (mshell_ConsumeWord(&pchArgs, "status") && mshell_AtEnd(pchArgs))) {
        _mvc_PrintStatus();
    } else {
        mshell_Write("Usage: rf448 start cap|res open|close <0..1000> | stop | status\r\n");
    }
}
MODUS_SHELL_CMD(rf448, _mvc_ShellRf448,
    "RF448: start cap|res open|close <0..1000> | stop | status");
#endif
