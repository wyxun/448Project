/**
 * @file instance.h
 * @brief Master AT32F415 static MDI resources.
 */
#ifndef M448_MASTER_AT32F415_MDI_INSTANCE_H
#define M448_MASTER_AT32F415_MDI_INSTANCE_H

#include "mdi/mdi.h"
#include "backend.h"

MDI_AT32_TIMER_BIND(dac_sample_timer, TMR1, TMR1_OVF_TMR10_IRQn, apb2_freq)
MDI_AT32_TICK_BIND(raw_tick, get_system_ticks)

#define AT32_MASTER_STREAM_BUFFER_SIZE 256U
extern mdi_at32_stream_state_t g_at32_master_dwin_stream;
extern mdi_at32_stream_state_t g_at32_master_board_link_stream;
extern uint8_t g_at32_master_dwin_tx[AT32_MASTER_STREAM_BUFFER_SIZE];
extern uint8_t g_at32_master_dwin_rx[AT32_MASTER_STREAM_BUFFER_SIZE];
extern uint8_t g_at32_master_board_link_tx[AT32_MASTER_STREAM_BUFFER_SIZE];
extern uint8_t g_at32_master_board_link_rx[AT32_MASTER_STREAM_BUFFER_SIZE];

#define AT32_MASTER_DAC_PINS(X, V, ...) \
    X(V, 0, 7, 0) X(V, 1, 6, 0) \
    X(V, 2, 5, 0) X(V, 3, 4, 0) \
    X(V, 4, 3, 0) X(V, 5, 2, 0) \
    X(V, 6, 1, 0) X(V, 7, 0, 0)
#define AT32_MASTER_DAC_PORTS(X, V) \
    X(V, GPIOA, AT32_MASTER_DAC_PINS)
MDI_AT32_IO_BIND(dac_parallel, 8, AT32_MASTER_DAC_PORTS)

#define AT32_MASTER_AB_PINS(X, V, ...) X(V, 0, 2, 0)
#define AT32_MASTER_AB_PORTS(X, V) X(V, GPIOB, AT32_MASTER_AB_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(dac_ab, 1, AT32_MASTER_AB_PORTS, MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_WR_PINS(X, V, ...) X(V, 0, 1, 0)
#define AT32_MASTER_WR_PORTS(X, V) X(V, GPIOB, AT32_MASTER_WR_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(dac_wr, 1, AT32_MASTER_WR_PORTS, MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_OE_PINS(X, V, ...) X(V, 0, 0, 0)
#define AT32_MASTER_OE_PORTS(X, V) X(V, GPIOB, AT32_MASTER_OE_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(dac_oe, 1, AT32_MASTER_OE_PORTS, MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_LED_PA(X, V, ...) X(V, 5, 8, 0)
#define AT32_MASTER_LED_PB(X, V, ...) \
    X(V, 0, 15, 0) X(V, 6, 7, 0) X(V, 7, 8, 0) X(V, 8, 9, 0)
#define AT32_MASTER_LED_PC(X, V, ...) \
    X(V, 1, 6, 0) X(V, 2, 7, 0) X(V, 3, 8, 0) X(V, 4, 9, 0)
#define AT32_MASTER_LED_PORTS(X, V) \
    X(V, GPIOA, AT32_MASTER_LED_PA) X(V, GPIOB, AT32_MASTER_LED_PB) \
    X(V, GPIOC, AT32_MASTER_LED_PC)
MDI_AT32_IO_BIND_OUTPUT_CAPS(leds, 9, AT32_MASTER_LED_PORTS,
                             MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_STATUS_LED_PINS(X, V, ...) X(V, 0, 3, 0)
#define AT32_MASTER_STATUS_LED_PORTS(X, V) \
    X(V, GPIOB, AT32_MASTER_STATUS_LED_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(status_led, 1, AT32_MASTER_STATUS_LED_PORTS,
                             MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_TLED_PINS(X, V, ...) X(V, 0, 12, 0)
#define AT32_MASTER_TLED_PORTS(X, V) X(V, GPIOA, AT32_MASTER_TLED_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(tled, 1, AT32_MASTER_TLED_PORTS,
                             MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_HEAT_PINS(X, V, ...) \
    X(V, 0, 10, 0) X(V, 1, 11, 0)
#define AT32_MASTER_HEAT_PORTS(X, V) X(V, GPIOB, AT32_MASTER_HEAT_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(heat, 2, AT32_MASTER_HEAT_PORTS,
                             MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_BIO_POWER_PINS(X, V, ...) X(V, 0, 11, 0)
#define AT32_MASTER_BIO_POWER_PORTS(X, V) \
    X(V, GPIOA, AT32_MASTER_BIO_POWER_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(bio_power, 1, AT32_MASTER_BIO_POWER_PORTS,
                             MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_BUZZER_PINS(X, V, ...) X(V, 0, 2, 0)
#define AT32_MASTER_BUZZER_PORTS(X, V) X(V, GPIOD, AT32_MASTER_BUZZER_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(buzzer, 1, AT32_MASTER_BUZZER_PORTS,
                             MDI_IO_CAP_OUTPUT)

#define AT32_MASTER_KEY_PINS(X, V, ...) \
    X(V, 0, 14, 0) X(V, 1, 13, 0) X(V, 2, 12, 0)
#define AT32_MASTER_KEY_PORTS(X, V) X(V, GPIOB, AT32_MASTER_KEY_PINS)
MDI_AT32_IO_BIND_INPUT_CAPS(keys, 3, AT32_MASTER_KEY_PORTS, MDI_IO_CAP_INPUT)

#define AT32_MASTER_ENCODER_PINS(X, V, ...) \
    X(V, 0, 4, 0) X(V, 1, 5, 0) X(V, 2, 6, 0)
#define AT32_MASTER_ENCODER_PORTS(X, V) X(V, GPIOB, AT32_MASTER_ENCODER_PINS)
MDI_AT32_IO_BIND_INPUT_CAPS(encoder, 3, AT32_MASTER_ENCODER_PORTS,
                            MDI_IO_CAP_INPUT)

/* ADC/DMA state is owned by this MDI instance, like the reference
 * peripheral_template/mdi/state.c.  The binding below only consumes these
 * addresses and fixes the scan layout at compile time. */
#define AT32_MASTER_ADC_INPUT_COUNT 5U
#define AT32_MASTER_ADC_SAMPLE_COUNT 10U
#define AT32_MASTER_ADC_SLOT_COUNT 2U
#define AT32_MASTER_ADC_BLOCK_SIZE \
    (AT32_MASTER_ADC_INPUT_COUNT * AT32_MASTER_ADC_SAMPLE_COUNT)
extern volatile uint16_t
    g_at32_master_adc_dma[AT32_MASTER_ADC_SLOT_COUNT *
                          AT32_MASTER_ADC_INPUT_COUNT *
                          AT32_MASTER_ADC_SAMPLE_COUNT];
extern volatile uint32_t g_at32_master_adc_dma_published;
extern volatile uint32_t g_at32_master_adc_dma_consumed;
extern volatile uint32_t g_at32_master_adc_sequence;
extern volatile bool g_at32_master_adc_valid;
extern volatile uint32_t g_at32_master_adc_rate_hz;
extern volatile uint16_t
    g_at32_master_adc_mean[AT32_MASTER_ADC_INPUT_COUNT];
extern volatile bool g_at32_master_adc_dma_busy;

#define AT32_MASTER_ADC_CHANNELS(X, ...) \
    X(__VA_ARGS__, ntc1, 0) X(__VA_ARGS__, ntc2, 1) \
    X(__VA_ARGS__, ntc3, 2) X(__VA_ARGS__, ntc4, 3) \
    X(__VA_ARGS__, bioadc, 4)
MDI_ADC_DMA_FLAG_BIND(adc_dma, g_at32_master_adc_dma_published,
                      g_at32_master_adc_dma_consumed,
                      AT32_MASTER_ADC_SLOT_COUNT)
MDI_ADC_MEAN_GROUP_BIND(adc_mean, adc_dma, g_at32_master_adc_dma,
                        AT32_MASTER_ADC_SAMPLE_COUNT,
                        AT32_MASTER_ADC_INPUT_COUNT,
                        AT32_MASTER_ADC_CHANNELS,
                        g_at32_master_adc_mean,
                        g_at32_master_adc_sequence,
                        g_at32_master_adc_valid,
                        g_at32_master_adc_rate_hz,
                        1000000U)
MDI_AT32_ADC_DMA_TRIGGER_BIND(
    adc_mean, ADC1, DMA1_CHANNEL1, g_at32_master_adc_dma,
    AT32_MASTER_ADC_BLOCK_SIZE, g_at32_master_adc_dma_published,
    g_at32_master_adc_dma_busy)
MDI_ADC_CHANNEL_MEAN_BIND(adc_ntc1, adc_mean, g_at32_master_adc_mean[0],
                          0xFFFFU, 0U, g_at32_master_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(adc_ntc2, adc_mean, g_at32_master_adc_mean[1],
                          0xFFFFU, 0U, g_at32_master_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(adc_ntc3, adc_mean, g_at32_master_adc_mean[2],
                          0xFFFFU, 0U, g_at32_master_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(adc_ntc4, adc_mean, g_at32_master_adc_mean[3],
                          0xFFFFU, 0U, g_at32_master_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(adc_bioadc, adc_mean, g_at32_master_adc_mean[4],
                          0xFFFFU, 0U, g_at32_master_adc_sequence)

MDI_AT32_STREAM_BIND(dwin_stream, g_at32_master_dwin_stream)
MDI_AT32_STREAM_BIND(board_link_stream, g_at32_master_board_link_stream)

#endif /* M448_MASTER_AT32F415_MDI_INSTANCE_H */
