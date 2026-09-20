#include "mdi/instance.h"

void at32_mdi_static_binding_probe(void)
{
    MDI_IO_Write(dac_parallel, 0x5AU);
    MDI_IO_Write(dac_ab, 1U);
    MDI_IO_Write(dac_wr, 0U);
    MDI_IO_Write(dac_wr, 1U);
    MDI_IO_Write(status_led, 1U);
    MDI_IO_Write(tled, 0U);
    MDI_IO_Write(heat, 0x3U);
    MDI_IO_Write(bio_power, 1U);
    MDI_IO_Write(buzzer, 0U);
    (void)MDI_ADC_Read(adc_ntc1, &(mdi_adc_value_t){0});
    (void)MDI_ADC_MeanUpdate(adc_mean);
    MDI_ADC_Start(adc_mean);
    (void)MDI_STREAM_Write(dwin_stream, (const uint8_t *)0, 0U);
    (void)MDI_STREAM_Read(board_link_stream, (uint8_t *)0, 0U);
    (void)MDI_STREAM_Available(dwin_stream);
    (void)MDI_STREAM_IsBusy(board_link_stream);
}
