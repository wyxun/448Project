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
#endif
