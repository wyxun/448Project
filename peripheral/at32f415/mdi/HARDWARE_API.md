# AT32F415 Master 板级硬件 API 总览

本文档是旧 `HW` 硬件职责表的静态 MDI 对应物。它只描述当前板级资源和调用方式，不创建运行时硬件对象。

通用原语定义在 `modus/src/mdi/core` 和 `modus/src/mdi/feature`；本板的资源 token 在 [instance.h](instance.h)，芯片绑定在 [backend.h](backend.h)。

## 调用约定

| 硬件类别 | 主要 API | 说明 |
|---|---|---|
| GPIO 输入/输出 | `MDI_IO_Read(R)`、`MDI_IO_Write(R, value)`、`MDI_IO_WriteMasked(R, mask, value)` | `R` 是本表中的静态资源 token |
| 定时器 | `MDI_TIMER_SetFrequency(R, hz)`、`MDI_TIMER_Start(R)`、`MDI_TIMER_Stop(R)`、`MDI_TIMER_IsRunning(R)` | 控制外设定时器，不用于 raw tick 换算 |
| 原始 tick | `MDI_TICK_Now(raw_tick)`、`MDI_TICK_Elapsed(raw_tick, limit)` | tick 是 CPU 原始周期数，无时间单位 |
| ADC/DMA | `MDI_ADC_Start(R)`、`MDI_ADC_MeanUpdate(R)`、`MDI_ADC_Read(R, &value)` | DMA 中断只发布块，均值由板级 `mdi_Service()` 处理 |
| 字节流 | `MDI_STREAM_Write(R, p, n)`、`MDI_STREAM_Read(R, p, n)`、`MDI_STREAM_Available(R)`、`MDI_STREAM_IsBusy(R)` | 串口由板级 stream 绑定提供 |

## 时基与 DAC

| 职责 | MDI 资源 | 物理资源 | 调用方式 | 备注 |
|---|---|---|---|---|
| DAC 采样定时器 | `dac_sample_timer` | TMR1，APB2，更新中断 `TMR1_OVF_TMR10_IRQHandler` | `MDI_TIMER_SetFrequency` / `MDI_TIMER_Start` / `MDI_TIMER_Stop` | 默认由 `mircoelec` 配置为 320 kHz；中断经 `mircoelec_IsrStep()` 传入对象后进入 `dac_driver_IsrStep(ptDac)` |
| raw tick | `raw_tick` | `perf_counter::get_system_ticks()` | `MDI_TICK_Now(raw_tick)` | 当前 Master 主频按 backend 配置为 144 MHz |
| DAC 并行数据 | `dac_parallel` | PA7、PA6、PA5、PA4、PA3、PA2、PA1、PA0 | `MDI_IO_Write(dac_parallel, value)` | 逻辑 bit0 对应 PA7，逻辑 bit7 对应 PA0；连接 TLC7528 DA0~DA7 |
| DAC 通道选择 | `dac_ab` | PB2，TLC7528 DAC/AB | `MDI_IO_Write(dac_ab, value)` | 由 DAC driver 内部选择载波/包络通道 |
| DAC 写脉冲 | `dac_wr` | PB1，TLC7528 WR# | `MDI_IO_Write(dac_wr, value)` | DAC driver 高速路径直接翻转 |
| DAC 输出使能 | `dac_oe` | PB0，TXB0108 OE | `MDI_IO_Write(dac_oe, value)` | 低有效/高阻控制按板级电路约定 |

上层波形接口由 `dac_driver` 提供：`dac_driver_SetCarrierFrequency()`、`dac_driver_SetEnvelopeFrequency()`、`dac_driver_Start()`、`dac_driver_Stop()`。应用不直接操作 DAC 通道时序。

## GPIO 输出与输入

| 职责 | MDI 资源 | 物理引脚 | 调用方式 | 备注 |
|---|---|---|---|---|
| 多路 LED | `leds` | PB15、PC6、PC7、PC8、PC9、PA8、PB7、PB8、PB9 | `MDI_IO_Write(leds, value)` | 逻辑索引顺序由 `AT32_MASTER_LED_*` 宏定义；具体颜色/状态名称以原理图网络标号为准 |
| 状态 LED | `status_led` | PB3 | `MDI_IO_Write(status_led, 0/1)` | 普通 GPIO 电平输出 |
| TLED | `tled` | PA12 | `MDI_IO_Write(tled, 0/1)` | 普通 GPIO 电平输出 |
| 发热控制 | `heat` | PB10（RTIN1）、PB11（RTIN2） | `MDI_IO_Write(heat, value)` | 低频 PWM 由应用产生，不绑定专用 PWM 外设 |
| 微电供电使能 | `bio_power` | PA11（RL0） | `MDI_IO_Write(bio_power, 0/1)` | 普通 GPIO 电平输出 |
| 蜂鸣器 | `buzzer` | PD2（BUZZ） | `MDI_IO_Write(buzzer, 0/1)` | 普通 GPIO 电平输出 |
| 按键 | `keys` | PB14（KEY1）、PB13（KEY2）、PB12（KEY3） | `MDI_IO_Read(keys)` | 返回右对齐的 3 位逻辑电平 |
| 编码器 | `encoder` | PB4（A）、PB5（B）、PB6（按下） | `MDI_IO_Read(encoder)` | 返回右对齐的 3 位逻辑电平；编码器解释属于扩展 MDI/driver |

## ADC/DMA

ADC1 使用软件触发、DMA 双槽、每通道 10 次采样后求均值。DMA 完成中断只调用发布原语；板级 `mdi_Service()` 按项目需要调用 `MDI_ADC_MeanUpdate(adc_mean)` 并安排下一次采样。
当前板级服务配置为 100 Hz，采样周期按 `AT32F415_CORE_CLOCK_HZ` 在编译期换算为原始 tick。

| MDI 资源 | ADC 顺序 | 物理引脚 | ADC 通道 | 读取方式 | 用途 |
|---|---:|---|---:|---|---|
| `adc_ntc1` | 0 | PC0（NTC1OUT） | ADC_IN10 | `MDI_ADC_Read(adc_ntc1, &value)` / `MDI_OP(adc_ntc1, _adc_ReadFast)()` | NTC1 采集 |
| `adc_ntc2` | 1 | PC1（NTC2OUT） | ADC_IN11 | 同上 | NTC2 采集 |
| `adc_ntc3` | 2 | PC2（NTC3OUT） | ADC_IN12 | 同上 | NTC3 采集 |
| `adc_ntc4` | 3 | PC3（NTC4OUT） | ADC_IN13 | 同上 | NTC4 采集 |
| `adc_bioadc` | 4 | PC5（BIOADC） | ADC_IN15 | 同上 | 微电输出 AD 采集，具体业务由上层解释 |

ADC 组资源：`adc_dma` 用于块发布/消费，`adc_mean` 用于均值更新和采样频率设置。后台调度属于板级 `mdi_Service()`，应用层只通过 `MDI_ADC_Read()` 读取已发布结果。

## 串口与字节流

| MDI 资源 | 外设 | 引脚/配置 | 调用方式 | 上层职责 |
|---|---|---|---|---|
| `dwin_stream` | USART1 | 115200 baud；引脚由 `port_sys.c` 配置 | `MDI_STREAM_*` | `dwin_driver` / `mvc_ui` 负责迪文协议和页面变量 |
| `board_link_stream` | USART3 | PC10/PC11，4800 baud | `MDI_STREAM_*` | `board_link_driver` 负责两板隔离通信；协议暂由上层确定 |

## 初始化与中断边界

- `peripheral_Init()` 在 [port_sys.c](../port_sys.c) 中完成时钟、GPIO、USART、ADC/DMA 和 TMR1 的芯片初始化。
- `state.c` 只提供 ADC DMA、均值和串口流所需的板级共享存储；`service.c` 提供强定义的
  `mdi_Service()` / `mdi_Clock()` 回调，二者共同组成运行时 MDI 状态，不再生成旧式 `HW` 对象。
- `TMR1_OVF_TMR10_IRQHandler` 清除更新标志后调用 `mircoelec_IsrStep()`，由 Class 明确传入 DAC Driver 对象。
- `DMA1_Channel1_IRQHandler` 只发布 ADC DMA 所有权；均值计算在板级 `mdi_Service()` 完成。
- `modus_Run()` 先调用板级 `mdi_Service()`，再运行 MODUS 对象；`modus_Clock()` 先调用板级 `mdi_Clock()`，再运行对象时钟回调。
- `USART1_IRQHandler` 和 `USART3_IRQHandler` 只进入对应 stream 的中断宏。
- 应用层不访问 `GPIOx`、`ADC1`、`DMA1`、`TMR1` 寄存器，也不通过旧 `HW` 结构体访问硬件。
