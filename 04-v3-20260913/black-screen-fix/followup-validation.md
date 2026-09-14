# 继续实板验证记录

本轮继续检查已烧录的 `61d7d1c8...0659a6fe` 固件，没有更换固件或修改显示参数。

## 显示输出

通过 USB JTAG 只读采样显示 DMA 计数，间隔 10.1904 秒完成 610 帧，约 **59.86 帧/秒**。`g_esp_mipi_dsi_underruns` 两次均为 0。这个计数对应显示 DMA 扫描输出，不代表桌面每秒重绘次数。

从 GPIO 寄存器确认：GPIO26（背光）与 GPIO27（屏幕复位）均启用输出，输出值均为高。它证明软件设置和寄存器状态正确，不等同于对引脚电压、屏幕供电或实体画面的测量。

证据：`evidence/display-health.json`、`evidence/display-health-0.log`、`evidence/display-health-1.log`。寄存器地址来自已烧录 ELF 和 P4 `hw_ver3` 头文件。

## 运行状态与复位排查

首次状态查询显示 uptime 为 1217.48 秒，桌面与 NSH 正常，内核堆仍空闲 317,496 字节。之后调试期间出现 uptime 回落，因此没有将整段调试过程记为连续无复位测试。

随后使用同一个串口连接、关闭 DTR/RTS、不连接 JTAG，连续采样 90 秒：

| 采样 | uptime（秒） | 用户堆占用（字节） | 内核堆占用（字节） |
|---|---:|---:|---:|
| 0 | 77.43 | 3,793,920 | 38,416 |
| 1 | 107.50 | 3,793,920 | 38,416 |
| 2 | 137.46 | 3,793,920 | 38,416 |
| 3 | 167.45 | 3,793,920 | 38,416 |

观察期间没有 ROM 重启日志，uptime 连续增长，desktop 和 esp_timer 均存活。证据：`evidence/runtime-monitor.json` 和 `evidence/runtime-monitor.log`。

后续查询 uptime 为 342.37 秒；`LP_CLKRST_RESET_CAUSE`（`0x50111010`）读回字节为 `c1 eb 15 02`，即 `0x0215ebc1`。按 HPCORE0 字段 `(value >> 7) & 0x3f` 解码为 **0x17：USB UART chip reset**。这证明最近一次复位属于 USB 串口复位，不能据此追溯所有先前复位事件。证据：`evidence/console-reset-reason.log`。

`diagnostics/query_console.py` 已改为在打开串口前设置 `DTR=False`、`RTS=False`，与原采集脚本一致。新增的 `monitor_runtime.py` 与 `check_display_health.py` 可复查上述状态。

软件启动、帧缓冲图像、DMA 输出和背光寄存器状态均已验证；实体屏幕目视结果仍待用户确认。本轮未进行触摸交互或长时间负载测试。

## 未打开串口监视器的启动

通过 USB 控制线复位后立即关闭串口，不发送回车或任何控制台数据。等待启动后通过 JTAG 读取：`g_usbserial_priv.host_active=false`，`g_lcd_ready=true`，`video_running=true`，系统为 `OSINIT_IDLELOOP`，desktop、esp_timer 与 NSH 任务均存活。证明桌面启动不依赖串口输入激活；USB 主机仍连接，本测试不等同于拔掉 USB 后独立供电或断电重启。证据：`evidence/no-console-startup.txt`。
