/**
 * @file heater_control.c
 * @brief Master RTIN1/RTIN2 heater control driver.
 */
#include "heater_control.h"

#include "mdi/instance.h"
#include "perf_counter.h"

#include <stddef.h>
#include <string.h>

static uint16_t _heater_control_GetDutyPermille(
    const input_driver_temperature_t *ptTemperature,
    int16_t nTargetDeciC,
    bool *pbOverTemp)
{
    int32_t nErrorDeciC;

    *pbOverTemp = (ptTemperature->eResult == INPUT_DRIVER_RESULT_OK)
        && (ptTemperature->nDeciC >= HEATER_CONTROL_OVERTEMP_DECI_C);

    if ((ptTemperature->eResult != INPUT_DRIVER_RESULT_OK)
        || *pbOverTemp
        || (ptTemperature->nDeciC >= nTargetDeciC)) {
        return 0U;
    }

    nErrorDeciC = nTargetDeciC - ptTemperature->nDeciC;
    if (nErrorDeciC >= HEATER_CONTROL_PROPORTIONAL_BAND_DECI_C) {
        return HEATER_CONTROL_DUTY_FULL_PERMILLE;
    }

    return (uint16_t)((nErrorDeciC * HEATER_CONTROL_DUTY_FULL_PERMILLE)
                      / HEATER_CONTROL_PROPORTIONAL_BAND_DECI_C);
}

heater_control_result_t heater_control_Init(
    heater_control_t *ptThis,
    const input_driver_temperature_t *ptTemperature)
{
    if (ptThis == NULL) {
        return HEATER_CONTROL_RESULT_NULL;
    }

    (void)memset(ptThis, 0, sizeof(*ptThis));
    MDI_IO_Write(heat, 0U);
    if (ptTemperature == NULL) {
        return HEATER_CONTROL_RESULT_NULL;
    }

    ptThis->ptTemperature = ptTemperature;
    return HEATER_CONTROL_RESULT_OK;
}

void heater_control_Start(heater_control_t *ptThis)
{
    (void)memset(ptThis->ahwDutyPermille, 0,
                 sizeof(ptThis->ahwDutyPermille));
    ptThis->hwPwmElapsedMs = 0U;
    ptThis->bEnabled = true;
    MDI_IO_Write(heat, 0U);
}

void heater_control_Stop(heater_control_t *ptThis)
{
    ptThis->bEnabled = false;
    (void)memset(ptThis->ahwDutyPermille, 0,
                 sizeof(ptThis->ahwDutyPermille));
    MDI_IO_Write(heat, 0U);
}

void heater_control_Run(heater_control_t *ptThis)
{
    input_driver_temperature_t tTemperature = {0};
    bool bOverTemp = false;
    uint8_t chOverTempMask = 0U;
    uint16_t hwDutyPermille = 0U;

    if (!perfc_is_time_out_ms(HEATER_CONTROL_UPDATE_PERIOD_MS,
                              &ptThis->lControlDeadline, true)) {
        return;
    }

    tTemperature = ptThis->ptTemperature[HEATER_CONTROL_CHANNEL_BIO];
    hwDutyPermille = _heater_control_GetDutyPermille(
        &tTemperature, HEATER_CONTROL_BIO_TARGET_DECI_C, &bOverTemp);
    ptThis->ahwDutyPermille[HEATER_CONTROL_CHANNEL_BIO] =
        ptThis->bEnabled ? hwDutyPermille : 0U;
    if (bOverTemp) {
        chOverTempMask |= HEATER_CONTROL_OVERTEMP_BIO_MASK;
    }

    tTemperature = ptThis->ptTemperature[HEATER_CONTROL_CHANNEL_COMMON];
    hwDutyPermille = _heater_control_GetDutyPermille(
        &tTemperature, HEATER_CONTROL_COMMON_TARGET_DECI_C, &bOverTemp);
    ptThis->ahwDutyPermille[HEATER_CONTROL_CHANNEL_COMMON] =
        ptThis->bEnabled ? hwDutyPermille : 0U;
    if (bOverTemp) {
        chOverTempMask |= HEATER_CONTROL_OVERTEMP_COMMON_MASK;
    }

    ptThis->chOverTempMask = chOverTempMask;
}

void heater_control_Clock(heater_control_t *ptThis)
{
    uint32_t wOutputs = 0U;
    unsigned i;

    if (!ptThis->bEnabled) {
        MDI_IO_Write(heat, 0U);
        return;
    }

    if (++ptThis->hwPwmElapsedMs >= HEATER_CONTROL_PWM_PERIOD_MS) {
        ptThis->hwPwmElapsedMs = 0U;
    }

    for (i = 0U; i < HEATER_CONTROL_CHANNEL_COUNT; ++i) {
        uint32_t wOnTimeMs =
            ((uint32_t)HEATER_CONTROL_PWM_PERIOD_MS
             * ptThis->ahwDutyPermille[i])
            / HEATER_CONTROL_DUTY_FULL_PERMILLE;
        if ((uint32_t)ptThis->hwPwmElapsedMs < wOnTimeMs) {
            wOutputs |= 1UL << i;
        }
    }

    MDI_IO_Write(heat, wOutputs);
}

