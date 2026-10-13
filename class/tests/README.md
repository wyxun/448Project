# Class 独立验证

在 `M448_master` 下运行：

```powershell
./class/tests/run.ps1
./class/tests/run.ps1 -Crc 0
./class/tests/run.ps1 -Module dac
./class/tests/run.ps1 -Module dwin
./class/tests/run.ps1 -Module mircoelec
./class/tests/run_waveform.ps1
./class/tests/run.ps1 -Module mvc
./class/tests/run.ps1 -Module mvc -SerialShell 1
./class/tests/run.ps1 -Module ui
./class/tests/run.ps1 -Module link
./class/tests/run.ps1 -Module adc_service
./class/tests/run.ps1 -Module adc_service -AdcBoard client
```

脚本优先使用工程现有 MinGW GCC，否则查找 PATH 中的 gcc。每个模块只编译自身及实际依赖，生成独立可执行文件到 `build/`，使用 `-Wall -Wextra -Werror`。无需 MCU 或额外测试框架。

| 模块 | 验证行为 |
|---|---|
| DAC | 空配置、频率边界、相位步进、临界区恢复、停止失败后的安全输出及故障保留 |
| DWIN | 配置校验、两实例超时隔离、重新初始化、收到新字节重置超时、逐字节拆包、短帧、CRC 错误、每轮处理上限及后续粘包、溢出丢弃与恢复、发送短写 |
| Microelec | 未初始化回调、基类绑定、启动失败不注册、注册失败停止资源、正常启动 |
| MVC | 未初始化回调、基类绑定、实际库 PT 重复推进、串口失败锁存、shell 模式不抢读 USART1 |
| UI | 依赖校验、页面提交成功后更新状态、提交失败保留原页面 |
| ADC Service | 100 Hz 周期、首次立即启动、DMA 忙保留到期请求、成功后重计时、64 位 tick 回绕 |
| Board Link | 读写字节数、读数据一致性、传输失败统计、短写背压、溢出状态及有效字节数量 |

测试在编译期替换 MDI、时钟和 MODUS 注册边界；PT 使用真实 `perfc_task_pt.h` 宏。对象内部状态仅在白盒断言中读取。此验证不覆盖 MCU 中断延迟、GPIO 波形、DAC 输出质量或屏幕实际响应。

`run_waveform.ps1` 编译真实 DAC 驱动、mircoelec、独立 waveform
模块及 mwaveform 协议，只替换硬件和 RTT 传输边界。解码输出帧
检查 50/500 Hz 理想调幅、每 32 次 DAC 更新一个同步样本、
100000 ns 时间间隔、幅度端点、改频清理旧批次、停止、非法频率、
启动失败以及 RTT 拥塞时非阻塞丢样本。

## 调整后的调用约定

- DWIN 回调接收 `const dwin_driver_frame_t *`。数据指针仅在回调期间有效，不得缓存该指针或重入同一解析器。
- DWIN 超时使用对象自己的 `lRxDeadline`，初始化及收到新字节时重置；`wFrameTimeoutMs` 为毫秒。
- `mvc_ui` 保留页面切换及 VP 接收处理；通用读写直接调用 DWIN。空 `mvc_ui_Run` 已删除。
- MVC 持有板间通信模块；协议仍待定义，本轮不添加协议或空调度。
- DAC 停止失败仍关闭 OE、锁存 ERROR；后续 Stop 可继续尝试停定时器，但不会清除故障。
- Microelec 先启动 DAC，再调用基类注册；注册失败会关闭 DAC。其必要的 Run/Clock 回调没有增加无意义 PT。
