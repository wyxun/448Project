/**
 * @file service.c
 * @brief Client board-level MDI maintenance services.
 * @author Modus project
 * @date 2026-10-12
 */

#include "mdi/instance.h"

#define AT32_CLIENT_ADC_SAMPLE_RATE_HZ 100U
#define AT32_CLIENT_ADC_SAMPLE_PERIOD_TICKS \
    ((mdi_tick_t)(AT32F421_CORE_CLOCK_HZ / AT32_CLIENT_ADC_SAMPLE_RATE_HZ))

/* Foreground-only board scheduler; DMA ISR owns only publication state. */
static mdi_tick_t s_wAdcLastTick;
static bool s_bAdcPrimed;

/**
 * @brief Reset the foreground schedule before MODUS objects start.
 * @return None.
 */
void mdi_Init(void)
{
    s_wAdcLastTick = 0U;
    s_bAdcPrimed = false;
}

/**
 * @brief Reserved board clock hook; UART queues need no periodic tick.
 * @return None.
 */
void mdi_Clock(void)
{
    /* UART stream reads are immediate; no idle-gap clock is required. */
}

/**
 * @brief Publish means and start due ADC blocks without waiting for DMA.
 * @return None.
 * @note Foreground-only; a failed start preserves the due request.
 */
void mdi_Service(void)
{
    mdi_status_t eStatus = MDI_ADC_MeanUpdate(client_adc_mean);
    mdi_tick_t wNow = MDI_TICK_Now(raw_tick);

    if ((eStatus != MDI_OK) && (eStatus != MDI_BUSY)
        && (eStatus != MDI_OVERRUN)) {
        return;
    }
    if (!s_bAdcPrimed || (wNow - s_wAdcLastTick
            >= AT32_CLIENT_ADC_SAMPLE_PERIOD_TICKS)) {
        eStatus = MDI_ADC_Start(client_adc_mean);
        if (eStatus == MDI_OK) {
            s_bAdcPrimed = true;
            /* A busy DMA keeps the due request pending until next service. */
            s_wAdcLastTick = wNow;
        }
    }
}
