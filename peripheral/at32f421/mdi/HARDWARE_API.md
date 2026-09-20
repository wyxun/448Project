# AT32F421 Client 板级硬件 API 总览

本文档是旧 `HW` 硬件职责表的静态 MDI 对应物。它只描述当前 Client 板级资源和调用方式，不创建运行时硬件对象。

通用原语定义在 `modus/src/mdi/core` 和 `modus/src/mdi/feature`；本板的资源 token 在 [instance.h](instance.h)，芯片绑定在 [backend.h](backend.h)。

## 调用约定

| 硬件类别 | 主要 API | 说明 |
|---|---|---|
| GPIO 输入/输出 | `MDI_IO_Read(R)`、`MDI_IO_Write(R, value)`、`MDI_IO_WriteMasked(R, mask, value)` | `R` 是本表中的静态资源 token |
| PWM | `MDI_PWM_SetDuty(R, duty)`、`MDI_PWM_Stage(R, &frame)` | 只更新比较寄存器；定时器、死区和输出使能由板级初始化负责 |
| 原始 tick | `MDI_TICK_Now(raw_tick)`、`MDI_TICK_Elapsed(raw_tick, limit)` | tick 是 CPU 原始周期数，无时间单位 |
| ADC/DMA | `MDI_ADC_Start(R)`、`MDI_ADC_MeanUpdate(R)`、`MDI_ADC_Read(R, &value)` | DMA 中断只发布块，均值由板级 `mdi_Service()` 处理 |
| 字节流 | `MDI_STREAM_Write(R, p, n)`、`MDI_STREAM_Read(R, p, n)`、`MDI_STREAM_Available(R)`、`MDI_STREAM_IsBusy(R)` | 当前 Client 串口绑定为 USART1 |

## PWM 与 GPIO 输出

| 职责 | MDI 资源 | 物理资源 | 调用方式 | 备注 |
|---|---|---|---|---|
| DAC/包络 PWM | `dac_pwm` | TMR3 CH1，DACPWM（PA5） | `MDI_PWM_SetDuty(dac_pwm, duty)` | 周期由 `port_sys.c` 初始化为 ARR=999 |
| 448K PWM | `pwm1` | TMR1 CH1 / CH1C，PWM1P、PWM1N | `MDI_PWM_SetDuty(pwm1, duty)` | 周期由 `port_sys.c` 初始化为 ARR=106；互补输出、死区和输出使能属于板级初始化 |
| IR2110 关断 | `pt_sd` | PB1（SD） | `MDI_IO_Write(pt_sd, 0/1)` | 普通 GPIO 电平控制 |
| 阻抗匹配继电器 | `pt_relay` | PB7（RL1）、PB6（RL2）、PB5（RL3）、PB4（RL4）、PB3（RL5）、PA15（RL6） | `MDI_IO_Write(pt_relay, value)` | 逻辑数组 0 对应 RL1，数组 5 对应 RL6 |
| 状态 LED | `pt_led_status` | PA11（STATELED） | `MDI_IO_Write(pt_led_status, 0/1)` | 普通 GPIO 电平输出 |

## ADC/DMA

ADC1 使用软件触发、DMA 双槽、每通道 10 次采样后求均值。DMA 完成中断只调用发布原语；板级 `mdi_Service()` 按项目需要调用 `MDI_ADC_MeanUpdate(client_adc_mean)` 并安排下一次采样。
当前板级服务配置为 100 Hz，采样周期按 `AT32F421_CORE_CLOCK_HZ` 在编译期换算为原始 tick。

| MDI 资源 | ADC 顺序 | 物理引脚 | ADC 通道 | 读取方式 | 用途 |
|---|---:|---|---:|---|---|
| `client_ivadc` | 0 | PA0（IVADC） | ADC_IN0 | `MDI_ADC_Read(client_ivadc, &value)` / `_adc_ReadFast` | 48 V 可调电压反馈采集 |
| `client_ntc12` | 1 | PA1（NTC12OUT） | ADC_IN1 | 同上 | NTC12 采集 |
| `client_ntc11` | 2 | PA2（NTC11OUT） | ADC_IN2 | 同上 | NTC11 采集 |
| `client_rfv` | 3 | PA3（RF0V） | ADC_IN3 | 同上 | 反馈电压采集 |
| `client_rfi` | 4 | PA4（RF0I） | ADC_IN4 | 同上 | 反馈电流采集 |

ADC 组资源：`client_adc_dma` 用于块发布/消费，`client_adc_mean` 用于均值更新和采样频率设置。后台调度属于板级 `mdi_Service()`，应用层只通过 `MDI_ADC_Read()` 读取已发布结果。

## 串口与时基

| MDI 资源 | 外设 | 引脚/配置 | 调用方式 | 上层职责 |
|---|---|---|---|---|
| `serial_stream` | USART1 | 115200 baud；PA9 TX、PA10 RX | `MDI_STREAM_*` | Client 应用串口服务 |
| `raw_tick` | `perf_counter::get_system_ticks()` | AT32F421 主频 48 MHz | `MDI_TICK_Now(raw_tick)` | 原始 CPU 周期参考，不提供毫秒/微秒单位 |

## 初始化与中断边界

- `peripheral_Init()` 在 [port_sys.c](../port_sys.c) 中完成时钟、GPIO、USART1、ADC/DMA、TMR1 和 TMR3 初始化。
- `state.c` 只提供 ADC DMA、均值和串口流所需的板级共享存储；`service.c` 提供强定义的
  `mdi_Service()` / `mdi_Clock()` 回调，二者共同组成运行时 MDI 状态，不再生成旧式 `HW` 对象。
- `DMA1_Channel1_IRQHandler` 只发布 ADC DMA 所有权；均值计算在板级 `mdi_Service()` 完成。
- `modus_Run()` 先调用板级 `mdi_Service()`，再运行 MODUS 对象；`modus_Clock()` 先调用板级 `mdi_Clock()`，再运行对象时钟回调。
- `USART1_IRQHandler` 只进入 `serial_stream` 的中断宏。
- 应用层不访问 `GPIOx`、`ADC1`、`DMA1`、`TMR1`、`TMR3` 寄存器，也不通过旧 `HW` 结构体访问硬件。
