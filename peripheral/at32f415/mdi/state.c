/**
 * @file state.c
 * @brief Master AT32F415 ADC/DMA instance storage.
 */
#include "instance.h"

mdi_at32_stream_state_t g_at32_master_dwin_stream;
mdi_at32_stream_state_t g_at32_master_board_link_stream;
uint8_t g_at32_master_dwin_tx[AT32_MASTER_STREAM_BUFFER_SIZE];
uint8_t g_at32_master_dwin_rx[AT32_MASTER_STREAM_BUFFER_SIZE];
uint8_t g_at32_master_board_link_tx[AT32_MASTER_STREAM_BUFFER_SIZE];
uint8_t g_at32_master_board_link_rx[AT32_MASTER_STREAM_BUFFER_SIZE];

volatile uint16_t g_at32_master_adc_dma[
    AT32_MASTER_ADC_SLOT_COUNT * AT32_MASTER_ADC_BLOCK_SIZE];
volatile uint32_t g_at32_master_adc_dma_published = 0U;
volatile uint32_t g_at32_master_adc_dma_consumed = 0U;
volatile uint32_t g_at32_master_adc_sequence = 0U;
volatile bool g_at32_master_adc_valid = false;
volatile uint32_t g_at32_master_adc_rate_hz = 100U;
volatile uint16_t g_at32_master_adc_mean[AT32_MASTER_ADC_INPUT_COUNT];
volatile bool g_at32_master_adc_dma_busy = false;
