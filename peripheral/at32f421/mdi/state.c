/**
 * @file state.c
 * @brief Client AT32F421 ADC/DMA instance storage.
 */
#include "instance.h"

mdi_at32_stream_state_t g_at32_client_serial_stream;
uint8_t g_at32_client_serial_tx[AT32_CLIENT_STREAM_BUFFER_SIZE];
uint8_t g_at32_client_serial_rx[AT32_CLIENT_STREAM_BUFFER_SIZE];

volatile uint16_t g_at32_client_adc_dma[
    AT32_CLIENT_ADC_SLOT_COUNT * AT32_CLIENT_ADC_BLOCK_SIZE];
volatile uint32_t g_at32_client_adc_dma_published = 0U;
volatile uint32_t g_at32_client_adc_dma_consumed = 0U;
volatile uint32_t g_at32_client_adc_sequence = 0U;
volatile bool g_at32_client_adc_valid = false;
volatile uint32_t g_at32_client_adc_rate_hz = 100U;
volatile uint16_t g_at32_client_adc_mean[AT32_CLIENT_ADC_INPUT_COUNT];
volatile bool g_at32_client_adc_dma_busy = false;
