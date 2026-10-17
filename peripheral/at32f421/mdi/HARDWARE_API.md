# AT32F421 Client 板级硬件 API 总览

本文档是旧 `HW` 硬件职责表的静态 MDI 对应物。它只描述当前 Client 板级资源和调用方式，不创建运行时硬件对象。

通用原语定义在 `modus/src/mdi/core` 和 `modus/src/mdi/feature`；本板的资源 token 在 [instance.h](instance.h)，芯片绑定在 [backend.h](backend.h)。

当前工作区 Modus 版本定义为 **0.6.1.4**；此驱动接入保留已有子模块改动。

## 调用约定

| 硬件类别 | 主要 API | 说明 |
|---|---|---|
| GPIO 输入/输出 | `MDI_IO_Read(R)`、`MDI_IO_Write(R, value)`、`MDI_IO_WriteMasked(R, mask, value)` | `R` 是本表中的静态资源 token |
| PWM | `MDI_PWM_Stage(R, &frame)`、`MDI_PWM_Enable(pwm1, bool)`、`MDI_PWM_SafeStop(R)`、`MDI_PWM_Commit(dac_pwm)` | compare 使用 timer tick；载波固定，DACPWM 支持安全启停和周期边界更新 |
| 原始 tick | `MDI_TICK_Now(raw_tick)` | tick 是 CPU 原始周期数；基准由调用者持有，旧 `MDI_TICK_Elapsed` 已移除 |
| ADC/DMA | `MDI_ADC_Start(R)`、`MDI_ADC_MeanUpdate(R)`、`MDI_ADC_Read(R, &value)` | DMA 中断只发布块，均值由板级 `mdi_Service()` 处理 |
| 字节流 | `MDI_STREAM_Write(R, p, n, &count)`、`MDI_STREAM_Read(R, p, n, &count)`、`MDI_STREAM_Available(R)`、`MDI_STREAM_IsBusy(R)` | 当前 Client 串口绑定为 USART1 |

## PWM 与 GPIO 输出

| 职责 | MDI 资源 | 物理资源 | 调用方式 | 备注 |
|---|---|---|---|---|
| 母线能量 PWM | `dac_pwm` | TMR3 CH1，DACPWM（PA6） | `MDI_PWM_Stage` + `MDI_PWM_Commit` | TMR3=96 MHz；ARR=999、PSC=19，4.8 kHz；compare 0..1000；SafeStop 强制高电平，经 NPN 反相后为零控制量 |
| 固定 448K PWM | `pwm1` | TMR1 CH1 / PA8 PWM1N；CH1C / PA7 PWM1P | `MDI_PWM_Enable` / `MDI_PWM_SafeStop` | TMR1=96 MHz；ARR=213，compare=107，deadtime=11（约 114.6 ns）；实际 448.598 kHz、两路高电平约 44.86%；停机两路低电平 |
| IR2110 关断 | `pt_sd` | PB1 经 S8050 反相到 IR2110 SD | `MDI_IO_Write(pt_sd, 0/1)` | MDI 逻辑 1=关断、0=放行；PB1 物理低=关断、高=放行 |
| 匹配及头选择继电器 | `pt_relay` | bit0～5 对应客板 RL1～RL6，依次映射 PA15、PB3、PB4、PB5、PB6、PB7 | `MDI_IO_WriteMasked(pt_relay, 0x38, value)` | RL1～RL3 匹配预留；RL4～RL6（PB5～PB7）选头，CAP=0x10、RES=0x38，Stop=0 |
| 状态 LED | `pt_led_status` | PA11（STATELED） | `MDI_IO_Write(pt_led_status, 0/1)` | 普通 GPIO 电平输出 |

## ADC/DMA

ADC1 使用软件触发、DMA 双槽、每通道 10 次采样后求均值。DMA 完成中断只调用发布原语；板级 `mdi_Service()` 按项目需要调用 `MDI_ADC_MeanUpdate(client_adc_mean)` 并安排下一次采样。
当前板级服务配置为 100 Hz，采样周期在 [service.c](service.c) 按 `AT32F421_CORE_CLOCK_HZ` 编译期换算为原始 tick。首次 Service 立即启动；DMA 忙时保留到期请求，成功启动后重新计时。实际频率受前台调度延迟限制。均值绑定不再伪装成采样频率控制；当前没有运行时调频接口。

| MDI 资源 | ADC 顺序 | 物理引脚 | ADC 通道 | 读取方式 | 用途 |
|---|---:|---|---:|---|---|
| `client_ivadc` | 0 | PA0（IVADC） | ADC_IN0 | `MDI_ADC_Read(client_ivadc, &value)` | 48 V 可调电压反馈采集 |
| `client_ntc12` | 1 | PA1（NTC12OUT） | ADC_IN1 | 同上 | NTC12 采集 |
| `client_ntc11` | 2 | PA2（NTC11OUT） | ADC_IN2 | 同上 | NTC11 采集 |
| `client_rfv` | 3 | PA3（RF0V） | ADC_IN3 | 同上 | 反馈电压采集 |
| `client_rfi` | 4 | PA4（RF0I） | ADC_IN4 | 同上 | 反馈电流采集 |

ADC 组资源：`client_adc_dma` 用于块发布/消费，`client_adc_mean` 只用于均值更新。后台调度属于板级 `mdi_Service()`，应用层只通过 `MDI_ADC_Read()` 读取已发布结果。通道使用 `MDI_ADC_CHANNEL_VIEW_BIND`，读取不会进行均值计算；首次发布前返回 `MDI_BUSY`，有效结果是 12 位 ADC code。

## 串口与时基

| MDI 资源 | 外设 | 引脚/配置 | 调用方式 | 上层职责 |
|---|---|---|---|---|
| `serial_stream` | USART1 | 4800 baud；PA9 TX、PA10 RX | `MDI_STREAM_*` | Client 应用串口服务 |
| `raw_tick` | `perf_counter::get_system_ticks()` | AT32F421 HCLK 96 MHz | `MDI_TICK_Now(raw_tick)` | 原始 CPU 周期参考，不提供毫秒/微秒单位 |

Stream 读写返回 `mdi_status_t`，实际字节数从输出参数取得。空队列、零长度、短传输返回 `MDI_OK`；`MDI_OVERRUN` 可以伴随有效已读字节。调用方必须检查状态和数量。Read 立即返回已有字节，`mdi_Clock()` 不再推进空闲超时。

USART1 队列初始化成功后才启用 IRQ。接收溢出通过状态寄存器读取后再读取数据寄存器清除；发送完成同时检查 TDC 和 TDC 中断使能。USART1 为光耦板间链路，4800 baud，与 master USART3 对应。启用串口 shell 时该链路用于调试，不同时作为业务协议接收端。

继电器 MDI 逻辑位直接按客板 RL1～RL6 编号；选头使用 bit3～5，对应 PB5～PB7。

## 初始化与中断边界

- `peripheral_Init()` 在 [port_sys.c](../port_sys.c) 中完成时钟、GPIO、USART1、ADC/DMA、TMR1 和 TMR3 初始化。
- `state.c` 只提供 ADC DMA、均值和串口流所需的板级共享存储；`service.c` 提供强定义的
  `mdi_Init()` / `mdi_Service()` / `mdi_Clock()` 回调，三者共同组成运行时 MDI 状态，不再生成旧式 `HW` 对象。
- `DMA1_Channel1_IRQHandler` 只发布 ADC DMA 所有权；均值计算在板级 `mdi_Service()` 完成。
- `modus_Run()` 先调用板级 `mdi_Service()`，再运行 MODUS 对象；`modus_Clock()` 先调用板级 `mdi_Clock()`，再运行对象时钟回调。
- `USART1_IRQHandler` 只进入 `serial_stream` 的中断宏。
- 应用层不访问 `GPIOx`、`ADC1`、`DMA1`、`TMR1`、`TMR3` 寄存器，也不通过旧 `HW` 结构体访问硬件。
