#include "peripheral.h"
#include "at32f421.h"
#include "at32f421_usart.h"
#include "at32f421_adc.h"
#include "at32f421_dma.h"
#include "mdi/instance.h"

static void adc_init(void)
{
    adc_base_config_type tAdcBase;
    dma_init_type tDma;
    gpio_init_type tGpio;

    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
    crm_adc_clock_div_set(CRM_ADC_DIV_6);

    gpio_default_para_init(&tGpio);
    tGpio.gpio_mode = GPIO_MODE_ANALOG;
    tGpio.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 |
                      GPIO_PINS_3 | GPIO_PINS_4;
    gpio_init(GPIOA, &tGpio);

    dma_reset(DMA1_CHANNEL1);
    dma_default_para_init(&tDma);
    tDma.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    tDma.buffer_size = AT32_CLIENT_ADC_BLOCK_SIZE;
    tDma.memory_base_addr = (uint32_t)g_at32_client_adc_dma;
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
    tAdcBase.ordinary_channel_length = AT32_CLIENT_ADC_INPUT_COUNT;
    adc_base_config(ADC1, &tAdcBase);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_0, 1U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_1, 2U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_2, 3U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_3, 4U, ADC_SAMPLETIME_239_5);
    adc_ordinary_channel_set(ADC1, ADC_CHANNEL_4, 5U, ADC_SAMPLETIME_239_5);
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
    g_at32_client_adc_dma_published = 0U;
    g_at32_client_adc_dma_consumed = 0U;
    g_at32_client_adc_sequence = 0U;
    g_at32_client_adc_valid = false;
    g_at32_client_adc_dma_busy = false;
}

static void pwm_init(void)
{
    tmr_output_config_type tOutput;
    tmr_brkdt_config_type tBreak;

    crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);

    tmr_base_init(TMR1, 106U, 0U);
    tmr_output_default_para_init(&tOutput);
    tOutput.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    tOutput.oc_output_state = TRUE;
    tOutput.occ_output_state = TRUE;
    tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_1, &tOutput);
    tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, 53U);
    tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_1C, &tOutput);
    tmr_brkdt_default_para_init(&tBreak);
    tBreak.deadtime = 2U;
    tmr_brkdt_config(TMR1, &tBreak);
    tmr_output_enable(TMR1, TRUE);
    tmr_counter_enable(TMR1, FALSE);

    tmr_base_init(TMR3, 999U, 0U);
    tmr_output_channel_config(TMR3, TMR_SELECT_CHANNEL_1, &tOutput);
    tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_1, 0U);
    tmr_output_enable(TMR3, TRUE);
    tmr_counter_enable(TMR3, FALSE);
}

/**
 * @brief 内部调用：AT32F421 系统时钟配置
 *
 *  AT32F421 内置 HICK 支持两种工作频率：
 *    - 默认 8 MHz（复位后）
 *    - 48 MHz（通过 misc2.hick_to_sclk=1 + misc1.hickdiv=1 使能）
 */
static void SystemClock_Config(void)
{
    /* 1. 等待 HICK 稳定 */
    while (crm_flag_get(CRM_HICK_STABLE_FLAG) == RESET);

    /* 2. Flash 等待周期：48 MHz 需 ≥1WS（使用宏 flash_psr_set） */
    flash_psr_set(FLASH_WAIT_CYCLE_1);

    /* 3. 使能 HICK 48 MHz 模式：
     *    misc2.hick_to_sclk = 1  → 允许 HICK 以 48 MHz 供给系统时钟
     *    misc1.hickdiv       = 1  → HICK 48 MHz 不分频（NODIV）
     */
    CRM->misc2_bit.hick_to_sclk = TRUE;
    CRM->misc1_bit.hickdiv       = TRUE;

    /* 4. AHB/APB 总线不分频 */
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb1_div_set(CRM_APB1_DIV_1);
    crm_apb2_div_set(CRM_APB2_DIV_1);

    /* 5. 切换系统时钟 → HICK */
    crm_sysclk_switch(CRM_SCLK_HICK);
    while (crm_sysclk_switch_status_get() != CRM_SCLK_HICK);

    /* 6. 更新全局时钟变量 */
    system_core_clock_update();
}

static void led_gpio_init(void)
{
    gpio_init_type gpio_init_struct;

    crm_periph_clock_enable(CRM_GPIOF_PERIPH_CLOCK, TRUE);
    gpio_default_para_init(&gpio_init_struct);

    /* PF7 — LED 指示 */
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
    gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_mode      = GPIO_MODE_OUTPUT;
    gpio_init_struct.gpio_pins      = GPIO_PINS_7;
    gpio_init_struct.gpio_pull      = GPIO_PULL_NONE;
    gpio_init(GPIOF, &gpio_init_struct);
}

static void output_gpio_init(void)
{
    gpio_init_type tGpio;

    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
    gpio_default_para_init(&tGpio);
    tGpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
    tGpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    tGpio.gpio_mode = GPIO_MODE_OUTPUT;
    tGpio.gpio_pull = GPIO_PULL_NONE;
    tGpio.gpio_pins = GPIO_PINS_1 | GPIO_PINS_3 | GPIO_PINS_4 |
                      GPIO_PINS_5 | GPIO_PINS_6 | GPIO_PINS_7;
    gpio_init(GPIOB, &tGpio);
    tGpio.gpio_pins = GPIO_PINS_11 | GPIO_PINS_15;
    gpio_init(GPIOA, &tGpio);
    gpio_bits_write(GPIOB, GPIO_PINS_1 | GPIO_PINS_3 | GPIO_PINS_4 |
                    GPIO_PINS_5 | GPIO_PINS_6 | GPIO_PINS_7, FALSE);
    gpio_bits_write(GPIOA, GPIO_PINS_11 | GPIO_PINS_15, FALSE);
}

static void pwm_gpio_init(void)
{
    gpio_init_type tGpio;

    gpio_default_para_init(&tGpio);
    tGpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
    tGpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    tGpio.gpio_mode = GPIO_MODE_MUX;
    tGpio.gpio_pull = GPIO_PULL_NONE;
    tGpio.gpio_pins = GPIO_PINS_6 | GPIO_PINS_7 | GPIO_PINS_8;
    gpio_init(GPIOA, &tGpio);
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE6, GPIO_MUX_2);
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE7, GPIO_MUX_2);
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE8, GPIO_MUX_2);
}
/**
 * @brief USART1 初始化
 */
static void usart1_init(void)
{
    gpio_init_type gpio_init_struct;

    crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    gpio_default_para_init(&gpio_init_struct);

    /* USART1 TX (PA9) */
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
    gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_mode      = GPIO_MODE_MUX;
    gpio_init_struct.gpio_pins      = GPIO_PINS_9;
    gpio_init_struct.gpio_pull      = GPIO_PULL_NONE;
    gpio_init(GPIOA, &gpio_init_struct);

    /* USART1 RX (PA10) */
    gpio_init_struct.gpio_pins = GPIO_PINS_10;
    gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
    gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
    gpio_init(GPIOA, &gpio_init_struct);

    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_1);
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE10, GPIO_MUX_1);

    nvic_irq_enable(USART1_IRQn, 1, 2);

    usart_init(USART1, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
    usart_transmitter_enable(USART1, TRUE);
    usart_receiver_enable(USART1, TRUE);
    usart_parity_selection_config(USART1, USART_PARITY_NONE);
    usart_hardware_flow_control_set(USART1, USART_HARDWARE_FLOW_NONE);

    /* The reference setup: */
    usart_interrupt_enable(USART1, USART_RDBF_INT, TRUE);
    usart_enable(USART1, TRUE);

    usart_flag_clear(USART1, USART_TDBE_FLAG);
    usart_flag_clear(USART1, USART_TDC_FLAG);
    usart_flag_clear(USART1, USART_RDBF_FLAG);
    usart_interrupt_enable(USART1, USART_TDC_INT, TRUE);

    MDI_AT32_STREAM_INIT(g_at32_client_serial_stream, USART1,
                         g_at32_client_serial_tx,
                         AT32_CLIENT_STREAM_BUFFER_SIZE,
                         g_at32_client_serial_rx,
                         AT32_CLIENT_STREAM_BUFFER_SIZE);
}

/**
 * @brief 系统级别的底层外设初始化总入口 (实现)
 *
 * 包装了所有针对当前平台（AT32F421）的初始化代码，将 main 函数与底层解耦。
 */
void peripheral_Init(void)
{
    /* 中断优先级分组：4位抢占优先级，0位响应优先级 */
    nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

    /* 1. 初始化系统时钟 HICK 48 MHz */
    SystemClock_Config();

    /* 2. debug led 初始化 */
    led_gpio_init();

    /* 3. 板级输出先置安全电平，再切换到定时器复用 */
    output_gpio_init();
    pwm_gpio_init();
    pwm_init();
    adc_init();

    /* 4. USART1 板间串口初始化 */
    usart1_init();

    /* 5. 启动 SysTick 1ms 中断 (48MHz / 1000 = 48000) */
    SysTick_Config(SystemCoreClock / 1000U);
}

/**
 * @brief 抽象的系统时钟获取函数
 */
uint32_t get_system_core_clock_hz(void)
{
    return SystemCoreClock; /* 返回系统时钟（AT32: #define 到 system_core_clock） */
}
