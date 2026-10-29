#include "input_driver.h"
#include "input_ntc_lut.h"
#include "mdi/instance.h"
#include "perf_counter.h"
#include <stddef.h>
#include <string.h>

static void _input_driver_UpdateTemperature(input_driver_temperature_t *ptTemp,
                                            mdi_status_t eStatus,
                                            uint32_t wCode)
{
    int16_t nTemperature;
    if (eStatus == MDI_BUSY) {
        ptTemp->eResult = INPUT_DRIVER_RESULT_NO_DATA;
        return;
    }
    if (eStatus != MDI_OK) {
        ptTemp->eResult = INPUT_DRIVER_RESULT_ADC_ERROR;
        return;
    }
    if ((wCode == 0U) || (wCode >= 4095U)) {
        ptTemp->eResult = INPUT_DRIVER_RESULT_NTC_INVALID;
        return;
    }
    nTemperature = s_nInputNtcTemperatureDeciC[wCode];
    if ((nTemperature < INPUT_DRIVER_NTC_MIN_DECI_C)
        || (nTemperature > INPUT_DRIVER_NTC_MAX_DECI_C)) {
        ptTemp->eResult = INPUT_DRIVER_RESULT_NTC_INVALID;
        return;
    }
    ptTemp->nDeciC = nTemperature;
    ptTemp->eResult = INPUT_DRIVER_RESULT_OK;
}

input_driver_result_t input_driver_Init(input_driver_t *ptThis)
{
    unsigned i;
    if (ptThis == NULL) { return INPUT_DRIVER_RESULT_NULL; }
    (void)memset(ptThis, 0, sizeof(*ptThis));
    for (i = 0; i < INPUT_DRIVER_NTC_COUNT; ++i) {
        ptThis->tTemperature[i].eResult = INPUT_DRIVER_RESULT_NO_DATA;
    }
    ptThis->bInitialized = true;
    return INPUT_DRIVER_RESULT_OK;
}

void input_driver_Run(input_driver_t *ptThis)
{
    mdi_adc_value_t tValue = {0};
    mdi_status_t eStatus;
    if ((ptThis == NULL) || !ptThis->bInitialized) { return; }
    if (!perfc_is_time_out_ms(INPUT_DRIVER_TEMPERATURE_PERIOD_MS,
                              &ptThis->lTemperatureDeadline, false)) { return; }
    ptThis->lTemperatureDeadline = 0;
    eStatus = MDI_ADC_Read(adc_ntc1, &tValue);
    _input_driver_UpdateTemperature(&ptThis->tTemperature[0], eStatus, tValue.wCode);
    eStatus = MDI_ADC_Read(adc_ntc2, &tValue);
    _input_driver_UpdateTemperature(&ptThis->tTemperature[1], eStatus, tValue.wCode);
    eStatus = MDI_ADC_Read(adc_ntc3, &tValue);
    _input_driver_UpdateTemperature(&ptThis->tTemperature[2], eStatus, tValue.wCode);
    eStatus = MDI_ADC_Read(adc_ntc4, &tValue);
    _input_driver_UpdateTemperature(&ptThis->tTemperature[3], eStatus, tValue.wCode);
}

void input_driver_Clock(input_driver_t *ptThis)
{
    uint8_t chPressed;
    unsigned i;
    if ((ptThis == NULL) || !ptThis->bInitialized) { return; }
    chPressed = (uint8_t)(~MDI_IO_Read(keys)) & 0x07U;
    for (i = 0; i < INPUT_DRIVER_KEY_COUNT; ++i) {
        const uint8_t chMask = (uint8_t)(1U << i);
        if (((chPressed ^ ptThis->chStableKeys) & chMask) == 0U) {
            ptThis->achKeyDebounce[i] = 0U;
        } else if (++ptThis->achKeyDebounce[i] >= INPUT_DRIVER_KEY_DEBOUNCE_MS) {
            ptThis->achKeyDebounce[i] = 0U;
            ptThis->chStableKeys ^= chMask;
            if ((ptThis->chStableKeys & chMask) != 0U) {
                ptThis->chPendingKeys |= chMask;
            }
        }
    }
}

input_driver_result_t input_driver_GetTemperature(
    const input_driver_t *ptThis, input_driver_ntc_t eNtc,
    input_driver_temperature_t *ptTemperature)
{
    if ((ptThis == NULL) || (ptTemperature == NULL)) { return INPUT_DRIVER_RESULT_NULL; }
    if (!ptThis->bInitialized) { return INPUT_DRIVER_RESULT_NOT_READY; }
    if ((unsigned)eNtc >= INPUT_DRIVER_NTC_COUNT) { return INPUT_DRIVER_RESULT_INVALID_ARGUMENT; }
    *ptTemperature = ptThis->tTemperature[eNtc];
    return ptTemperature->eResult;
}

input_driver_result_t input_driver_GetKeyValue(input_driver_t *ptThis,
                                               uint8_t *pchKeys)
{
    if ((ptThis == NULL) || (pchKeys == NULL)) { return INPUT_DRIVER_RESULT_NULL; }
    if (!ptThis->bInitialized) { return INPUT_DRIVER_RESULT_NOT_READY; }
    {
        perfc_global_interrupt_status_t tIrqStatus =
            perfc_port_disable_global_interrupt();
        *pchKeys = ptThis->chPendingKeys;
        ptThis->chPendingKeys = 0U;
        perfc_port_resume_global_interrupt(tIrqStatus);
    }
    return INPUT_DRIVER_RESULT_OK;
}
