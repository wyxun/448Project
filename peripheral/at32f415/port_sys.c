/**
 * @file port_sys.c
 * @brief AT32F415RBT7 clock, GPIO and serial initialization.
 * @author Codex
 * @date 2026-09-18
 */

#include "peripheral.h"
#include "at32f415.h"
#include "at32f415_adc.h"
#include "at32f415_dma.h"
#include "mdi/instance.h"

static void adc_init(void)
{
    adc_base_config_type tAdcBase;
    dma_init_type tDma;
    gpio_init_type tGpio;

    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
    crm_adc_clock_div_set(CRM_ADC_DIV_6);

    gpio_default_para_init(&tGpio);
    tGpio.gpio_mode = GPIO_MODE_ANALOG;
    tGpio.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 |
                      GPIO_PINS_3 | GPIO_PINS_5;
    gpio_init(GPIOC, &tGpio);

    dma_reset(DMA1_CHANNEL1);
    dma_default_para_init(&tDma);
    tDma.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    tDma.buffer_size = AT32_MASTER_ADC_BLOCK_SIZE;
    tDma.memory_base_addr = (uint32_t)g_at32_master_adc_dma;
    tDma.memory_data_width = DMA_MEMORY_DATA_WIDTH_HALFWORD;
    tDma.memory_inc_enable = TRUE;
    tDma.peripheral_base_addr = (uint32_t)&ADC1->odt;
    tDma.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
    tDma.priority = DMA_PRIORITY_HIGH;
    tDma.loop_mode_enable = FALSE;
    dma_init(DMA1_CHANNEL1, &tDma);
    dma_interrupt_enable(DMA1_CHANNEL1, DMA_FDT_INT | DMA_DTERR_INT, TRUE);
    nvic_irq_enable(DMA1_Channel1_IRQn, 1U, 0U);

    adc_reset(ADC1);
    adc_base_default_para_init(&tAdcBase);
    tAdcBase.sequence_mode = TRUE;
    tAdcBase.repeat_mode = FALSE;
    tAdcBase.data_align = ADC_RIGHT_ALIGNMENT;
    tAdcBase.ordinary_channel_length = AT32_MASTER_ADC_INPUT_COUNT;
    adc_base_config(ADC1, &tAdcBase);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_10, 1U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_11, 2U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_12, 3U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_13, 4U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_15, 5U, ADC_SAMPLETIME_239_5);
    adc_ordinary_conversion_trigger_set(ADC1,
                                        ADC12_ORDINARY_TRIG_SOFTWARE, TRUE);
    adc_dma_mode_enable(ADC1, TRUE);
    adc_enable(ADC1, TRUE);
    adc_calibration_init(ADC1);
    while (adc_calibration_init_status_get(ADC1) != RESET) { }
    adc_calibration_start(ADC1);
    while (adc_calibration_status_get(ADC1) != RESET) { }
    dma_channel_enable(DMA1_CHANNEL1, FALSE);
    adc_enable(ADC1, FALSE);
    g_at32_master_adc_dma_published = 0U;
    g_at32_master_adc_dma_consumed = 0U;
    g_at32_master_adc_sequence = 0U;
    g_at32_master_adc_valid = false;
    g_at32_master_adc_dma_busy = false;
}

static void system_clock_config(void)
{
    crm_reset();
    flash_psr_set(FLASH_WAIT_CYCLE_4);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
    while (crm_hext_stable_wait() == ERROR) {
    }
    crm_pll_config(CRM_PLL_SOURCE_HEXT_DIV, CRM_PLL_MULT_36);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
    while (crm_flag_get(CRM_PLL_STABLE_FLAG) != SET) {
    }
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb2_div_set(CRM_APB2_DIV_2);
    crm_apb1_div_set(CRM_APB1_DIV_2);
    crm_auto_step_mode_enable(TRUE);
    crm_sysclk_switch(CRM_SCLK_PLL);
    while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL) {
    }
    crm_auto_step_mode_enable(FALSE);
    system_core_clock_update();
}

static void gpio_init_output(gpio_type *ptPort, uint16_t hwPins)
{
    gpio_init_type tGpio;
    gpio_default_para_init(&tGpio);
    tGpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
    tGpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    tGpio.gpio_mode = GPIO_MODE_OUTPUT;
    tGpio.gpio_pull = GPIO_PULL_NONE;
    tGpio.gpio_pins = hwPins;
    gpio_init(ptPort, &tGpio);
    gpio_bits_write(ptPort, hwPins, FALSE);
}

static void gpio_init_input(gpio_type *ptPort, uint16_t hwPins)
{
    gpio_init_type tGpio;
    gpio_default_para_init(&tGpio);
    tGpio.gpio_mode = GPIO_MODE_INPUT;
    tGpio.gpio_pull = GPIO_PULL_NONE;
    tGpio.gpio_pins = hwPins;
    gpio_init(ptPort, &tGpio);
}

static void board_gpio_init(void)
{
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK, TRUE);

    gpio_init_output(GPIOA, GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 |
                     GPIO_PINS_3 | GPIO_PINS_4 | GPIO_PINS_5 |
                     GPIO_PINS_6 | GPIO_PINS_7 | GPIO_PINS_8 |
                     GPIO_PINS_11 | GPIO_PINS_12);
    gpio_init_output(GPIOB, GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 |
                     GPIO_PINS_3 | GPIO_PINS_7 | GPIO_PINS_8 |
                     GPIO_PINS_9 | GPIO_PINS_10 | GPIO_PINS_11);
    gpio_init_output(GPIOC, GPIO_PINS_6 | GPIO_PINS_7 | GPIO_PINS_8 |
                     GPIO_PINS_9);
    gpio_init_output(GPIOD, GPIO_PINS_2);
    gpio_init_input(GPIOB, GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_6 |
                    GPIO_PINS_12 | GPIO_PINS_13 | GPIO_PINS_14);
}

static void usart_config(usart_type *ptUsart, uint32_t wBaud,
                         IRQn_Type eIrq)
{
    crm_periph_clock_enable((ptUsart == USART1)
                                ? CRM_USART1_PERIPH_CLOCK
                                : CRM_USART3_PERIPH_CLOCK, TRUE);
    nvic_irq_enable(eIrq, 2U, 0U);
    usart_init(ptUsart, wBaud, USART_DATA_8BITS, USART_STOP_1_BIT);
    usart_transmitter_enable(ptUsart, TRUE);
    usart_receiver_enable(ptUsart, TRUE);
    usart_parity_selection_config(ptUsart, USART_PARITY_NONE);
    usart_hardware_flow_control_set(ptUsart, USART_HARDWARE_FLOW_NONE);
    usart_interrupt_enable(ptUsart, USART_RDBF_INT, TRUE);
    usart_interrupt_enable(ptUsart, USART_TDC_INT, TRUE);
    usart_enable(ptUsart, TRUE);
    usart_flag_clear(ptUsart, USART_TDBE_FLAG);
    usart_flag_clear(ptUsart, USART_TDC_FLAG);
    usart_flag_clear(ptUsart, USART_RDBF_FLAG);
}

static void serial_gpio_init(void)
{
    gpio_init_type tGpio;
    gpio_default_para_init(&tGpio);
    tGpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
    tGpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    tGpio.gpio_mode = GPIO_MODE_MUX;
    tGpio.gpio_pull = GPIO_PULL_NONE;
    tGpio.gpio_pins = GPIO_PINS_9 | GPIO_PINS_10;
    gpio_init(GPIOA, &tGpio);
    /* F415 vendor mux: PC10/PC11 is USART3. UART4 is PF4/PF5. */
    gpio_pin_remap_config(USART3_GMUX_0001, TRUE);
    tGpio.gpio_pins = GPIO_PINS_10 | GPIO_PINS_11;
    gpio_init(GPIOC, &tGpio);
}

static void serial_init(void)
{
    crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_USART3_PERIPH_CLOCK, TRUE);
    serial_gpio_init();
    usart_config(USART1, 115200U, USART1_IRQn);
    usart_config(USART3, 4800U, USART3_IRQn);
    MDI_AT32_STREAM_INIT(g_at32_master_dwin_stream, USART1,
                         g_at32_master_dwin_tx,
                         AT32_MASTER_STREAM_BUFFER_SIZE,
                         g_at32_master_dwin_rx,
                         AT32_MASTER_STREAM_BUFFER_SIZE);
    MDI_AT32_STREAM_INIT(g_at32_master_board_link_stream, USART3,
                         g_at32_master_board_link_tx,
                         AT32_MASTER_STREAM_BUFFER_SIZE,
                         g_at32_master_board_link_rx,
                         AT32_MASTER_STREAM_BUFFER_SIZE);
}

static void dac_sample_timer_init(void)
{
    crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);
}

void peripheral_Init(void)
{
    nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
    system_clock_config();
    board_gpio_init();
    serial_init();
    adc_init();
    dac_sample_timer_init();
    SysTick_Config(SystemCoreClock / 1000U);
}

uint32_t get_system_core_clock_hz(void)
{
    return SystemCoreClock;
}
