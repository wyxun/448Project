/**
 * @file instance.h
 * @brief Client AT32F421 static MDI resources.
 */
#ifndef M448_CLIENT_AT32F421_MDI_INSTANCE_H
#define M448_CLIENT_AT32F421_MDI_INSTANCE_H

#include "mdi/core/contract.h"
#include "mdi/core/tick.h"
#include "mdi/core/stream.h"
#include "mdi/core/bind.h"
#include "mdi/feature/adc_dma.h"
#include "mdi/feature/adc_mean.h"
#include "backend.h"

MDI_AT32_TICK_BIND(raw_tick, get_system_ticks)

#define AT32_CLIENT_STREAM_BUFFER_SIZE 256U
extern mdi_uart_stream_state_t g_at32_client_serial_stream;
extern uint8_t g_at32_client_serial_tx[AT32_CLIENT_STREAM_BUFFER_SIZE];
extern uint8_t g_at32_client_serial_rx[AT32_CLIENT_STREAM_BUFFER_SIZE];

#define AT32_CLIENT_SD_PINS(X, V, ...) X(V, 0, 1, 0)
#define AT32_CLIENT_SD_PORTS(X, V) X(V, GPIOB, AT32_CLIENT_SD_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(pt_sd, 1, AT32_CLIENT_SD_PORTS, MDI_IO_CAP_OUTPUT)

#define AT32_CLIENT_RELAY_PINS(X, V, ...) \
    X(V, 0, 7, 0) X(V, 1, 6, 0) X(V, 2, 5, 0) \
    X(V, 3, 4, 0) X(V, 4, 3, 0) X(V, 5, 15, 0)
#define AT32_CLIENT_RELAY_PORTS(X, V) \
    X(V, GPIOB, AT32_CLIENT_RELAY_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(pt_relay, 6, AT32_CLIENT_RELAY_PORTS, MDI_IO_CAP_OUTPUT)

#define AT32_CLIENT_LED_PINS(X, V, ...) X(V, 0, 11, 0)
#define AT32_CLIENT_LED_PORTS(X, V) X(V, GPIOA, AT32_CLIENT_LED_PINS)
MDI_AT32_IO_BIND_OUTPUT_CAPS(pt_led_status, 1, AT32_CLIENT_LED_PORTS, MDI_IO_CAP_OUTPUT)

/* Compare-register views for the two board PWM outputs.  Timer frequency,
 * dead-time and output enable remain owned by the AT32 initialization code;
 * these bindings expose only the fast compare staging operation. */
#define AT32_CLIENT_DAC_PWM_CHANNELS(X) X(duty, TMR3->c1dt, 999U)
MDI_PWM_REG_BIND(dac_pwm, AT32_CLIENT_DAC_PWM_CHANNELS)

#define AT32_CLIENT_PWM1_CHANNELS(X) X(duty, TMR1->c1dt, 106U)
MDI_PWM_REG_BIND(pwm1, AT32_CLIENT_PWM1_CHANNELS)

#define AT32_CLIENT_ADC_CHANNELS(X, ...) \
    X(__VA_ARGS__, ivadc, 0) X(__VA_ARGS__, ntc12, 1) \
    X(__VA_ARGS__, ntc11, 2) X(__VA_ARGS__, rfv, 3) \
    X(__VA_ARGS__, rfi, 4)

#define AT32_CLIENT_ADC_INPUT_COUNT 5U
#define AT32_CLIENT_ADC_SAMPLE_COUNT 10U
#define AT32_CLIENT_ADC_SLOT_COUNT 2U
#define AT32_CLIENT_ADC_BLOCK_SIZE \
    (AT32_CLIENT_ADC_INPUT_COUNT * AT32_CLIENT_ADC_SAMPLE_COUNT)
extern volatile uint16_t
    g_at32_client_adc_dma[AT32_CLIENT_ADC_SLOT_COUNT *
                          AT32_CLIENT_ADC_INPUT_COUNT *
                          AT32_CLIENT_ADC_SAMPLE_COUNT];
extern volatile uint32_t g_at32_client_adc_dma_published;
extern volatile uint32_t g_at32_client_adc_dma_consumed;
extern volatile uint32_t g_at32_client_adc_sequence;
extern volatile bool g_at32_client_adc_valid;
extern volatile uint32_t g_at32_client_adc_rate_hz;
extern volatile uint16_t
    g_at32_client_adc_mean[AT32_CLIENT_ADC_INPUT_COUNT];
extern volatile bool g_at32_client_adc_dma_busy;

MDI_ADC_DMA_FLAG_BIND(client_adc_dma, g_at32_client_adc_dma_published,
                      g_at32_client_adc_dma_consumed,
                      AT32_CLIENT_ADC_SLOT_COUNT)
MDI_ADC_MEAN_GROUP_BIND(client_adc_mean, client_adc_dma,
                        g_at32_client_adc_dma,
                        AT32_CLIENT_ADC_SAMPLE_COUNT,
                        AT32_CLIENT_ADC_INPUT_COUNT,
                        AT32_CLIENT_ADC_CHANNELS,
                        g_at32_client_adc_mean,
                        g_at32_client_adc_sequence,
                        g_at32_client_adc_valid,
                        g_at32_client_adc_rate_hz,
                        1000000U)
MDI_AT32_ADC_DMA_TRIGGER_BIND(
    client_adc_mean, ADC1, DMA1_CHANNEL1, g_at32_client_adc_dma,
    AT32_CLIENT_ADC_BLOCK_SIZE, g_at32_client_adc_dma_published,
    g_at32_client_adc_dma_busy)
MDI_ADC_CHANNEL_MEAN_BIND(client_ivadc, client_adc_mean,
                          g_at32_client_adc_mean[0], 0xFFFFU, 0U,
                          g_at32_client_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(client_ntc12, client_adc_mean,
                          g_at32_client_adc_mean[1], 0xFFFFU, 0U,
                          g_at32_client_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(client_ntc11, client_adc_mean,
                          g_at32_client_adc_mean[2], 0xFFFFU, 0U,
                          g_at32_client_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(client_rfv, client_adc_mean,
                          g_at32_client_adc_mean[3], 0xFFFFU, 0U,
                          g_at32_client_adc_sequence)
MDI_ADC_CHANNEL_MEAN_BIND(client_rfi, client_adc_mean,
                          g_at32_client_adc_mean[4], 0xFFFFU, 0U,
                          g_at32_client_adc_sequence)

MDI_AT32_UART_STREAM_BIND(serial_stream, g_at32_client_serial_stream, USART1)

#endif /* M448_CLIENT_AT32F421_MDI_INSTANCE_H */
