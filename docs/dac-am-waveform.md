# DAC 理想调幅 Waveform 与模拟运放硬件设计计算

实现源码：`class/mircoelec_waveform.c`，API：`class/mircoelec_waveform.h`。`mircoelec.c` 接入初始化、启停、错误停止和 DAC ISR 采样调用。

---

## 一、 硬件信号链路与运放逐级参数严格计算

硬件基于 **TLC7528CDWR**（双通道 8 位乘法型 DAC，5V 供电）与 **LM324DR**（四运放，$\pm 5\text{V}$ 双电源供电）构建两象限硬件调幅信号链，最终由 `4OUT` 送入后级功率放大电路。

### 1. 输入基准电压 $V_{\text{REFA}}$（原理图输入侧）
原理图左侧 `REFA` 基准分压网络：
- 上拉电阻 `R57 = 820Ω` 接 `+5V`；
- 下拉电阻 `R61 = 820Ω` 接地；
- 滤波电容 `C81 = 100nF`；

经典 $1:1$ 精密对称分压：
$$V_{\text{REFA}} = +5\text{V} \times \frac{R_{61}}{R_{57} + R_{61}} = +5\text{V} \times \frac{820\Omega}{820\Omega + 820\Omega} = \mathbf{2.50\text{V}}$$
TLC7528 的 DACA 参考基准电压被严格固定在 **$+2.50\text{V}$**。

### 2. 第一级跨阻放大（OpAmp 2: 引脚 5, 6, 7）
- 同相输入端 Pin 5（`2IN+`）接 GND（0V 虚地）；
- 反相输入端 Pin 6（`2IN-`）接 TLC7528 的 `OUTA`（Pin 2）；
- 反馈回路通过 TLC7528 内部匹配电阻 $R_{\text{FBA}}$（Pin 3，阻值 $R_{\text{FBA}} = R \approx 10\,\text{k}\Omega$）接回输出端 Pin 7（`2OUT`）。

#### 为什么输出范围是 $-2.5\text{V} \sim 0\text{V}$（负电压输出）？
1. **电流输出特性**：TLC7528 内部是 R-2R 梯形网络。当 `REFA` 输入 $+2.5\text{V}$ 正电压时，正电位驱动内部电流流出引脚 `OUTA`，流入运放反相端：
   $$I_{\text{OUTA}} = \frac{V_{\text{REFA}}}{R} \times \frac{D_A}{256} \quad (I_{\text{OUTA}} \ge 0)$$
2. **反相跨阻转换**：运放同相端接地构成 0V 虚地。根据基尔霍夫电流定律（KCL），流入反相端的电流无法进入运放内部，只能全部流经反馈电阻 $R_{\text{FBA}}$ 到达输出端 `2OUT`。
   $$I = \frac{V_{\text{虚地}} - V_{\text{2OUT}}}{R_{\text{FBA}}} = \frac{0\text{V} - V_{\text{2OUT}}}{R_{\text{FBA}}} \implies V_{\text{2OUT}} = - I_{\text{OUTA}} \times R_{\text{FBA}}$$
   为了把正电流抽向输出端，输出电位必须低于 0V 虚地，**因此输出必然为负电压**！

标准输出表达式：
$$V_{\text{2OUT}} = - V_{\text{REFA}} \times \frac{D_A}{256} = -2.50\text{V} \times \frac{D_A}{256}$$

- 当数字码 $D_A = 0$ 时：$V_{\text{2OUT}} = \mathbf{0.00\text{V}}$；
- 当数字码 $D_A = 128$ 时：$V_{\text{2OUT}} = \mathbf{-1.25\text{V}}$（正弦波交流中心）；
- 当数字码 $D_A = 255$ 时：$V_{\text{2OUT}} = \mathbf{-2.50\text{V}}$。

输出是一个以 **$-1.25\text{V}$** 为中心直流偏置、摆幅在 **$0\text{V} \sim -2.5\text{V}$** 之间的单极性负电压正弦波，峰峰值正好为 **$2.50\text{V}_{\text{pp}}$**。

### 3. 隔直滤波 `C85 (100nF)` + `R66 (33kΩ)` 与电压跟随器（OpAmp 1）
- `2OUT` 输出的负偏置信号接入串联隔直电容 `C85 (100nF)`，后级经 `R66 (33kΩ)` 泄放到地；
- `C85` 阻断了 $-1.25\text{V}$ 直流分量，交流正弦波自动围绕 0V 对称展开；
- 信号经 OpAmp 1（引脚 1, 2, 3，增益 $G = 1$ 的电压跟随器）缓冲后，送入 DACB 的参考基准输入端 `REFB`（Pin 18）：
  $$V_{\text{REFB}} = \pm 1.25\text{V} \quad (\mathbf{V_{\text{pp}} = 2.50\text{V}})$$

### 4. 第二级调幅（OpAmp 3）与末级缓冲输出（OpAmp 4 输出到 `4OUT`）
- DACB 以双极性交流信号 $V_{\text{REFB}}$（载波）作为参考基准，以数字码 $D_B$（包络）作为增益系数；
- OpAmp 3（引脚 8, 9, 10）完成 DACB 的跨阻放大，在 100% 满幅调制时理论输出 $\pm 1.25\text{V}$（$2.50\text{V}_{\text{pp}}$）；
- **末级运放 OpAmp 4（引脚 12, 13, 14）电路结构**：
  - 引脚 12（`4IN+`）接入前级 `3OUT` 信号；
  - 引脚 14（`4OUT`）**直接短路接回引脚 13（`4IN-`）**，构成**纯电压跟随器（增益 $G = 1.0$）**；
  - 旁边的 `R59 (2kΩ)` 与 `C75 (561pF)` 是并联在 `4OUT` 到地之间的假负载与高频滤波电容，不产生电压放大。
- **全链路纯理论设计值**：
  $$V_{\text{4OUT, 理论}} = 1.0 \times V_{\text{REFB}} = \mathbf{\pm 1.25\text{V}} \quad (\mathbf{V_{\text{pp}} = 2.50\text{V}})$$

### 5. 【存疑点】理论值 $2.50\text{V}_{\text{pp}}$ vs 实测值 $\approx 3.0\text{V}_{\text{pp}}$（正负 1.5V）
在实际示波器观察图二时，`4OUT` 测得峰值约在正负 1.5V（峰峰值 $V_{\text{pp}} \approx 3.0\text{V}$，约占 6 格），相比纯理论计算的 $2.50\text{V}_{\text{pp}}$ 偏高约 $20\%$。该存疑点已记录，可能由以下实际工程因素共同导致：

1. **示波器网格目测估读与载波过冲**：
   示波器档位为 $500\text{mV} / \text{div}$，理论 $\pm 1.25\text{V}$ 对应 $2.5$ 格。高频载波（2000Hz）在波峰处的微小纹波、探头接地环引入的瞬态毛刺与轻微过冲（Overshoot），容易使外包络峰顶目测显得接近 2.7~3 格（$\pm 1.4\text{V} \sim 1.5\text{V}$）；
2. **`+5V` 供电轨实际电压偏高**：
   若板载 `+5V`（来自 USB 或非高精度 DCDC）实际偏高（例如 $5.1\text{V} \sim 5.25\text{V}$），由 `R57/R61` 分压得到的基准 $V_{\text{REFA}}$ 也会同步抬高至 $2.55\text{V} \sim 2.63\text{V}$；
3. **元器件离散度与反馈网络串联电阻**：
   TLC7528 内部梯形网络阻抗标称约为 $R \approx 10\,\text{k}\Omega$（芯片制造批次公差可达 $\pm 20\%$），外围在 `RFBA`、`RFBB` 回路中串联了 `R70 (100Ω)`、`R68 (100Ω)`、`R58 (100Ω)` 等电阻，会对闭环放大比例产生细微影响；
4. **后续精确验证排查建议**：
   - 使用 4 位半以上万用表测量实际的 `+5V` 供电电压与 `REFA` 实际直流偏置；
   - 示波器开启硬件带宽限制（20MHz BW Limit），打开示波器自带的硬件参数统计测量项（`Vpp`、`Vtop`、`Vbase`），用数值读数替代人工肉眼估读。

---

## 二、 为什么 4OUT 绝不能设计成正负 5V？

很多工程师有疑问：“既然运放供电是 $\pm 5\text{V}$，为什么 `4OUT` 峰峰值只有 3.0V 而不是 10V？” 这由后级功放匹配和运放线性度共同决定：

### 1. 后级功率放大器 U17（TDA2030A）的增益匹配
看原理图右侧的功放级：
- TDA2030A 输出端 Pin 4 通过反馈电阻 `R84 = 10kΩ` 连接反相输入端 Pin 2；
- Pin 2 对地连接电阻 `R85 = 2kΩ`；
- TDA2030A 的闭环同相交流电压放大倍数为：
  $$A_v = 1 + \frac{R_{84}}{R_{85}} = 1 + \frac{10\,\text{k}\Omega}{2\,\text{k}\Omega} = 1 + 5 = \mathbf{6\text{ 倍}}$$

- **按 4OUT 激励信号（理论 $2.5\text{V}_{\text{pp}}$，实测约 $3.0\text{V}_{\text{pp}}$）计算**：
  经过功放 6 倍放大后，送入变压器前级的电压约为：
  $$V_{\text{TDA\_OUT}} = (2.5\text{V}_{\text{pp}} \sim 3.0\text{V}_{\text{pp}}) \times 6 = \mathbf{15.0\text{V}_{\text{pp}} \sim 18.0\text{V}_{\text{pp}}} \quad (\mathbf{\pm 7.5\text{V} \sim \pm 9.0\text{V}})$$
  TDA2030A 由 `+VCC` / `-VCC` 大电解电容滤波供电（通常供电在 $\pm 12\text{V} \sim \pm 15\text{V}$ 左右）。$\pm 7.5\text{V} \sim \pm 9.0\text{V}$ 的摆幅正好落在 TDA2030A 的高保真、低失真额定输出动态范围内，随后安全激励 T2 升压变压器。

- **反之，若 `4OUT` 输出达到 $\pm 5\text{V}$（$10\text{V}_{\text{pp}}$）**：
  后级放大 6 倍理论要求输出高达 **$60\text{V}_{\text{pp}}$**！这会直接导致 TDA2030A 深度饱和削顶、严重发热自激，甚至瞬间击穿烧毁功率管与变压器。

### 2. LM324 的非轨到轨输出动态裕量（Headroom）
LM324 是经典双极型（BJT）运算放大器，非 Rail-to-Rail 运放：
- 正向输出极限：$V_{\text{OH}} \le V_{\text{CC+}} - 1.5\text{V} \approx +3.5\text{V}$；
- 负向输出极限：$V_{\text{OL}} \ge V_{\text{CC-}} + 1.2\text{V} \approx -3.8\text{V}$；
在 $\pm 5\text{V}$ 双电源供电下，LM324 的无失真动态范围约为 $\pm 3.0\text{V}$。输出 $\pm 1.5\text{V}$ 正弦波时，距离电源饱和极限两端各有 **2.0V 左右的充裕 Headroom**，确保 2000Hz 高频载波无论在何种包络位置都不会发生压摆削顶失真。

---

## 三、 TLC7528 两象限乘法机理与通道分配原则

硬件电路强制决定了：**DACA 必须作为载波通道，DACB 必须作为包络通道！**

TLC7528 的乘法计算公式为：
$$V_{\text{OUT}} \propto V_{\text{REF}} \times \frac{D}{256}$$

- **参考端输入 $V_{\text{REF}}$**：经过 C85 隔直后为**双极性对称交流信号（可正可负）**；
- **数字码输入 $D$**：MCU 写入的无符号 8 位整数（$0 \sim 255$），**严格非负（单极性 $\ge 0$）**。

| 通道分配方案 | 物理机制 | 示波器实测波形表征 |
|---|---|---|
| **A 做包络，B 做载波**（错误） | $V_{\text{REF}}$ 是 50Hz 双极性包络，$D_B \ge 0$ 是 2000Hz 载波。因 $D_B$ 始终非负，输出符号被包络绑死：50Hz 正半周载波无法下潜到 0V 以下，负半周无法上升到 0V 以上。 | **严重削平畸变**：正半周向上立梳子、负半周向下立梳子，正弦载波无法跨过正负半周。 |
| **A 做载波，B 做包络**（正确） | $V_{\text{REF}}$ 是 2000Hz 双极性对称载波，$D_B \in [0, 255]$ 作为单极性增益调制系数。 | **理想调幅波**：高频载波自然跨越正负轴心对称摆动，包络平滑收放。 |

---

## 四、 调幅控制命令与等比缩放规范

通过串口 MShell 或 RTT 输入：

```text
dac start [dig|sin] <A_Hz> <B_Hz> [B_percent 0..100]
dac stop
```

- `[dig|sin]`：可选模式参数（缺省时向前兼容，默认 `sin`）。
  - `dig`：A相输出 50% 占空比、0~255 幅值拉满的数字方波。
  - `sin`：A相输出查表正弦波。
- `<A_Hz>`：DACA 频率（Hz），方波或包络频率。
- `<B_Hz>`：DACB 载波频率（Hz），例如 2000。
- `[B_percent]`：DACB 载波/直通峰值百分比（0..100，默认 100）。

### 包络缩放算法（以谷底 0 码为基准）
在 [`class/dac_driver.c`](file:///d:/1_project/13_448K/software/M448_master/class/dac_driver.c) 中：
```c
wScaledValue = (wBaseValue * (int32_t)chAmplitudePercent) / 100;
```
- **$100\%$ 时**：码值范围 $0 \sim 255 \implies$ 峰值 $\pm 1.50\text{V}$，谷底到 $0.0\text{V}$；
- **$50\%$ 时**：码值范围 $0 \sim 127 \implies$ 峰值等比减半至 $\pm 0.75\text{V}$，**谷底依然严格归零（$0.0\text{V}$）**；
- **$67\%$ 时**：码值范围 $0 \sim 170 \implies$ 峰值输出约 $\pm 1.00\text{V}$，谷底严格归零。

---

## 五、 RTT 理想调幅 Waveform 采集指南

启用 `MWAVEFORM_ENABLE` 编译后，启动 OpenOCD RTT，在 waveform 查看器连接 `127.0.0.1:9091`。

| 通道 | 含义 | 显示范围 | 说明 |
|---|---|---|---|
| EnvA | 调制包络样本 | 0..1 | 单极性包络归一化 |
| CarrB | 载波信号样本 | −1..1 | 双极性载波归一化 |
| AMSim | 理想调幅结果 | −1..1 | 理论包络乘积 |

- 采样率：200 kHz DAC 中断每 200 次抽取一次，RTT 显示采样率为 1 kHz（1 ms 间隔）。
- 传输格式：使用紧凑整数帧传输，ISR 仅消耗一次乘法和除法操作。
- 主机验证：`class/tests/run_waveform.ps1` 与 `class/tests/run.ps1`。
- 目标编译：`make.bat`；烧录：`make.bat flash`。
