/**
 * @file service.c
 * @brief Client board-level MDI maintenance services.
 */

#include "mdi/instance.h"

#define AT32_CLIENT_ADC_SAMPLE_RATE_HZ 100U
#define AT32_CLIENT_ADC_SAMPLE_PERIOD_TICKS \
    ((mdi_tick_t)(AT32F421_CORE_CLOCK_HZ / AT32_CLIENT_ADC_SAMPLE_RATE_HZ))

static bool s_bAdcConfigured;
static bool s_bAdcPrimed;

void mdi_Clock(void)
{
    MDI_AT32_STREAM_TICK_1MS(g_at32_client_serial_stream);
}

void mdi_Service(void)
{
    mdi_status_t eStatus;

    if (!s_bAdcConfigured) {
        eStatus = MDI_ADC_SetSampleFrequency(
            client_adc_mean, AT32_CLIENT_ADC_SAMPLE_RATE_HZ);
        if (eStatus == MDI_OK) {
            s_bAdcConfigured = true;
        }
    }

    (void)MDI_ADC_MeanUpdate(client_adc_mean);

    if (!s_bAdcPrimed || MDI_TICK_Elapsed(
            raw_tick, AT32_CLIENT_ADC_SAMPLE_PERIOD_TICKS)) {
        eStatus = MDI_ADC_Start(client_adc_mean);
        if (eStatus == MDI_OK) {
            s_bAdcPrimed = true;
        }
    }
}
