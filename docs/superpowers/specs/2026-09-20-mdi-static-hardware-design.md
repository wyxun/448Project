# MDI 静态硬件资源重构设计

## 目标

将 master 64-pin 和 client 32-pin 的硬件访问从运行时 `HW` 对象、`void *`、函数指针接口迁移到 MODUS 0.6.1.1 的编译期 MDI 资源绑定，同时保持 DAC 波形、ADC/DMA、DWIN 和两板通信的上层语义不变。

## 参考规则

以 `reference_src/peripheral_template` 和当前 `modus/src/mdi` 的 `core/`、`feature/`、`tests/` 为规范来源：

- 资源由 token 在编译期绑定，不通过设备编号或运行时对象查找。
- `core/` 只放跨芯片的契约；ADC/DMA、均值、DAC 波形属于 feature 或芯片组合。
- 高频路径不经过 `void *`、函数指针、虚表或通用分发。
- `legacy/` 只保留迁移期旧接口，不进入新实时路径。

## 目标分层

```text
应用 / class / 波形 feature
        │ MDI_* token API
        ▼
modus/src/mdi/core + feature
        │ 编译期资源绑定
        ▼
peripheral/at32f415/mdi/   master
peripheral/at32f421/mdi/   client
        │
        ▼
AT32 时钟、GPIO、ADC/DMA、PWM、USART、TMR 寄存器与 IRQ
```

`exmdi.h`、旧 `mdi_types.h` 和 `mdi_hw.h` 不再作为新硬件入口。旧文件只在迁移尚未完成的代码中临时保留。
板级 `state.c` 定义静态绑定需要的 DMA、均值和 Stream 存储，`service.c` 实现
`mdi_Service()` / `mdi_Clock()`；不再生成聚合 `HW` 对象。

## 并口 DAC

采用参考 demo 的 X-list 并口绑定。master 的 TLC7528 映射为逻辑 DA0～DA7 到 PA7～PA0：

```c
#define AT32_DAC_PINS(X, V, ...) \
    X(V, 0, 7, 0) X(V, 1, 6, 0) \
    X(V, 2, 5, 0) X(V, 3, 4, 0) \
    X(V, 4, 3, 0) X(V, 5, 2, 0) \
    X(V, 6, 1, 0) X(V, 7, 0, 0)

#define AT32_DAC_PORTS(X, V) X(V, GPIOA, AT32_DAC_PINS)
MDI_AT32_IO_BIND(dac_parallel, 8, AT32_DAC_PORTS)
```

AT32 后端用 `ODT` 读改写或等价的 `SCR/CLR` 原子写，只修改绑定的 PA0～PA7，保护 PA8、PA12 等其他输出。AB、WR、OE 分别绑定为单 bit 静态 IO。DAC 波形模块保留频率、相位、正弦表和 Start/Stop 语义，但硬件写入统一调用静态 MDI token，不再持有 `mdi_gpio_bus8_t`。

## ADC/DMA

沿用 `multi_peripheral_app.c` 的调用顺序，不修改公共 ADC feature 契约：

1. 芯片初始化配置 ADC 扫描顺序、DMA 双缓冲、DMA 中断和软件触发。
2. DMA 完成 ISR 只清标志、回收当前 DMA 状态并调用 `MDI_ADC_DMA_Publish(adc1_dma)`。
3. MODUS 进入板级 `mdi_Service()` 后调用 `MDI_ADC_MeanUpdate(adc1_mean)`，完成均值和稳定快照发布。
4. `mdi_Service()` 按编译期生成的 raw tick 周期调用 `MDI_ADC_Start(adc1_mean)` 启动下一批采样；
   应用 Run 不直接驱动 ADC service。
5. 应用通过 `MDI_ADC_Read(channel, &value)` 读取 NTC、BIOADC、RFV、RFI、IVADC 等通道视图。

ADC 缓冲区、发布/消费计数器、序列号和均值输出由芯片 `instance.h` 与 feature 绑定提供；AT32 外设初始化代码仍负责真实寄存器时序，但不再导出 `mdi_adc_t`、`mdi_adc_scan_t` 或 `at32_AdcValueHandle()`。

## PWM、GPIO、Timer 和 Stream

- client 的 TMR1 互补 PWM 用静态 PWM 帧、频率、提交和生命周期接口绑定；PA7/PA8 的互补关系由 AT32 后端负责。
- client 的 SD、RL1～RL6 和 LED 使用静态单 bit IO；继电器数组语义留在 client feature/class。
- master 的 LED、蜂鸣器、发热低频输出、按键和编码器使用静态 IO；编码器复杂协议另留 feature/driver 层。
- DWIN 与两板通信使用板级静态 Stream capability；协议状态机仍由 `dwin_driver`、`board_link_driver` 或 MVC 层负责。
- DAC 采样定时器作为 DAC feature 的芯片后端资源注入，ISR 直接绑定波形步进函数，不恢复通用 `mdi_timer_t` 回调对象。

`mdi_Clock()` 只处理 Stream 超时等 O(1) 轻量维护；DMA/USART ISR 只发布块所有权或搬运
字节，不能在中断中求均值、解析协议或调用应用逻辑。`mdi_Service()` 是通用的板级前台扩展点，
除 ADC 外也可以组合项目所需的其他硬件维护。

## 错误与所有权

MDI 绑定阶段用 `_Static_assert` 检查宽度、物理引脚重复、逻辑位覆盖和资源能力。运行时只返回公共 `mdi_status_t`；DMA 发布序号保护生产者与均值消费者的所有权，溢出返回 `MDI_OVERRUN`。硬件初始化失败在板级 init 阶段返回，不在第一次读写中隐式初始化。

## 验证

- 用现有 MDI contract、负编译和 codegen 测试验证静态绑定和能力约束。
- 分别构建 master 与 client，检查 AT32 目标编译和链接。
- 对 DAC ISR、ADC DMA ISR、PWM 提交和 GPIO 并口写生成汇编，确认没有运行时对象查找或间接调用。
- 在板级验证 ADC 通道顺序、DMA 双缓冲所有权、TLC7528 DA 位序、WR/AB 时序、TMR1 互补输出和 Stream 收发。
