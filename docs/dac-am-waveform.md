# DAC 理想调幅 Waveform

实现源码：`class/mircoelec_waveform.c`，API：
`class/mircoelec_waveform.h`。`mircoelec.c` 只接入初始化、
启停、错误停止和 DAC ISR 后的采样调用。

烧录启用 `MWAVEFORM_ENABLE` 的新固件，并启动 OpenOCD RTT 后，
在 waveform 查看器连接 `127.0.0.1:9091`。
通过 RTT MShell 或已配置的串口 MShell 输入：

```text
dac start 50 500
dac stop
```

`start` 同时开始 DAC 输出和三路波形采集；`stop` 停止新样本，
查看器可保留历史。再次 start 会清空 MCU 的旧批次和 RTT
波形缓冲，并重新发布通道和时间信息。

| 通道 | 含义 | 显示范围 |
|---|---|---|
| EnvA | DACA 的包络样本 A/255 | 0..1 |
| CarrB | DACB 的载波样本 2B/255−1 | −1..1 |
| AMSim | EnvA × CarrB 的理想调幅结果 | −1..1 |

数据来自同一次 DAC ISR 的两路已写入样本，没有额外的正弦
相位或独立定时器。320 kHz DAC 更新每 32 次抽样一次，
显示采样率为 10 kHz，样本间隔为 100 μs。
使用原始整数传输：EnvA 为 A，CarrB 为 `2B-255`，AMSim 为
`A*(2B-255)/2`（有符号除法向零截断）。通道 scale 分别为
255、255、32512.5，查看器据此显示归一化值。
ISR 的调幅计算仅需一次乘法和除以 2；相比理想样本乘积，
附加量化误差最多为 `1/65025`，约 0.0000154。

`50 500` 的包络周期是 20 ms，载波周期是 2 ms，
载波每周期约 20 个显示点。建议时间窗口至少 40 ms，
观察两个包络周期；可以隐藏 CarrB，只显示 EnvA 与 AMSim。

显示值为理论幅度，不代表示波器电压，也不包含运放、
交流耦合和负载的影响。0 Hz 保持 DAC 原有的 128 值，
因此归一化到双极性时存在约 1/255 的微小偏置。

随着频率上升，每周期的显示点会减少；当 A+B >= 5000 Hz
时日志提示混叠风险，DAC 仍接受原有的 0..20000 Hz 频率。
RTT 拥塞会丢显示样本并保留 gap/drop 统计，不等待传输。

release 构建禁用 MWAVEFORM_ENABLE，移除诊断对象、API 调用
和模块实现。串口 MShell 只改变命令传输通道，波形数据仍走 RTT。

主机验证：`class/tests/run_waveform.ps1`。
目标构建：`make.bat`。构建和主机检查不证明真机 ISR 时序，
新固件烧录后需再观察实际波形与采样丢失情况。
