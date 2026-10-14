# Client 448K 驱动使用

实现：`class/rf448_driver.h/.c`；应用对象：`class/mvc.h/.c`。
MVC 按值拥有 Driver；对象由 `MODUS_DECLARE_OBJECT(mvc, Mvc, ...)` 创建，ID 为 `MVC`。
默认构建使用 MVC 替换未连接实际硬件的 template 示例对象。

## 控制接口

```c
rf448_driver_Init(&driver, &cfg);
rf448_driver_Start(&driver, RF448_HEAD_CAP, 500U);
rf448_driver_Run(&driver);
rf448_driver_Stop(&driver);
rf448_driver_GetStatus(&driver, &status);
```

调用方必须检查返回值；`Start` 接受头类型和 0～1000 千分比能量目标，仅提交请求。
父 Class 在前台持续调用 `Run`。启动状态机完全在 Driver 内部：

```text
IDLE → 关断 RF、零能量 → 切换 RL4/RL5/RL6
     → RELAY_WAIT（默认 30 ms）
     → 设置 DACPWM → ENERGY_WAIT（默认 1 ms）
     → 启用固定互补载波 → 解除 SD → RUNNING
```

两段等待使用实例内 PT 游标与时间槽，不阻塞，不需要载波 ISR。
整个生命周期位于持续 PT 循环内：空闲等待请求、启动、运行中能量更新、停止清理、再次等待请求；BEGIN 前只有空对象、未初始化和故障守卫。
启动等待使用 `PERFC_PT_WAIT_UNTIL(STOP || perfc_is_time_out_ms(...))`，STOP 即使与截止时间同时到达也优先取消启动。
运行对象的 `lWaitDeadline` 为 perf_counter tick 截止时间，0 表示未启动；不是毫秒起点。两段等待顺序复用它，不借助文件静态时间槽。
`Run` 等待时返回 `fsm_rt_on_going`，就绪或空闲返回 `fsm_rt_cpl`，失败返回 `fsm_rt_err`。
`Start` 在等待或有未消费请求时返回 BUSY。RUNNING 中传入相同头类型可实时调节能量；传入另一头返回 BUSY，不改变输出。换头前先 Stop，并继续服务 Run 消费停止请求。
`Stop` 在返回前立即关断 RF、输出零能量、复位 RL4～RL6；下次 Run 在 PT 内结束本次工作、转 IDLE 并继续等待请求，不重置正常运行的 PT 游标。Stop 也取消尚未应用的能量更新。
运行中的能量更新由下一次 Run 提交，DACPWM preload 在下一 PWM 周期边界生效，最大约一个 4.8 kHz 周期（208.3 μs）；不重置计数器、触发软件更新事件、重启载波或动作继电器。模拟输出还受 GP8101、滤波和电源响应影响。
错误进入 ERROR 并保持锁存，Stop 不清除故障；修复后重新 MVC Init 可恢复驱动，并保留已注册的基类和同一对象 ID。
本模块独占板上的 RF 资源，不允许两个实例同时控制同一组 MDI token；真机只有 MVC 持有此驱动。

## MShell

```text
rf448 start cap 100
rf448 start cap 500
rf448 status
rf448 stop
rf448 start res 500
```

`cap` 为电容头，`res` 为电阻头。100 表示 10% 控制量，500 表示 50%。
状态与成功日志通过 mdebug 输出，需启用 I 级日志；使用现有 RTT 或串口 MShell 路径。
非法头、负数、超过 1000 或多余参数不会改变正在运行的输出。
`rf448 status` 的 state 为 0 未初始化、1 空闲、2 等继电器、3 等控制信号、4 运行、5 错误。
target 表示本次启动目标，不是实测输出；output 表示载波硬件使能状态，error 为锁存错误码。
首次 Start 的 result=0 只代表请求接受，RUNNING 日志才代表启动时序完成；运行中同头更新会打印 energy updated。

## 物理关系

| 功能 | 固定参数 / 接线 |
|---|---|
| 448K 载波 | 48 MHz / 107 = 448.598 kHz，TMR1 CH1/CH1C |
| PWM1N / PWM1P | PA8 主通道 / PA7 互补通道；比较值 53，死区 2 tick（约 41.67 ns） |
| 能量控制 DACPWM | PA6 / TMR3 CH1，4.8 kHz，周期 1000 tick，PSC=9 |
| IR2110 SD | PB1，1 关断、0 放行 |

载波频率固定，驱动没有调频或独立占空比 API。已有死区设置沿用，不代表已完成功率级时序验证。
图一 Q8 反相：MCU compare=`1000-energy`，MCU 占空比与能量目标反向。
零能量时 PA6 强制持续高，经过 Q8 后 GP8101 输入为低；不能将“停 PWM 后引脚低”当作零能量。
能量 1000 时 MCU 持续低，GP8101 输入持续高。按图中 0～5 V 模式，50% 目标对应理想控制电压约 2.5 V；这不等同于母线电压或实际输出功率。
1 ms 等待只针对控制信号，不判断开关电源是否已稳压。

| 头 | RL4 / K2 | RL5 / K3 | RL6 / K1 |
|---|---:|---:|---:|
| CAP | 0 | 1 | 0 |
| RES | 1 | 1 | 1 |

RL5 吸合选 448K 的 COM 回路；RL6 决定电容或电阻负极，RL4 同步选择 RF 通路。
控制使用掩码 0x38，仅修改后三路继电器；Stop 将后三路全部置 0、线圈释放。默认触点仍有连接，继电器复位不代表所有通路断开，也不保证母线已放电。
RF0I/RF0V 与 RL1/RL2/RL3 保留现有 MDI 资源，当前不执行闭环和阻抗匹配。

## 独立验证

```powershell
.\class\tests\run.ps1
.\class\tests\run.ps1 -Module driver
.\class\tests\run.ps1 -Module mvc
.\class\tests\run.ps1 -Module pwm
.\make.bat
```

主机检查使用真实 Driver、MVC、字符串解析和 PT 宏；只替换硬件、时钟和注册边界。
PWM 检查直接编译板级生命周期实现，并用寄存器 fake 检查固定参数、端点、启停和配置错误。
这些检查不能证明真实引脚、电平转换、继电器动作、母线稳定或功率输出；真机需烧录后用示波器和实测验证。
