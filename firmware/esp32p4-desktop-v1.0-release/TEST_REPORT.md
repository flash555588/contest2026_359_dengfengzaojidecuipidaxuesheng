# ESP32-P4 v1.0 实机验收报告

测试时间：2026-08-27（Asia/Shanghai）；目标板：ESP32-P4 Function EV Board；芯片：ESP32-P4 revision v1.0 / ECO2；串口：Silicon Labs CP210x，COM7，UART0 115200 8N1。

`nuttx.bin` 写入偏移 `0x2000`，esptool 报告写入 2947548 bytes，写后哈希校验通过。随后连续执行两次硬复位启动测试，均进入 NuttShell，未再出现 `CHIP_LP_WDT_RESET` 循环。

关键启动与运行证据：

```text
V1: PSRAM ready
NuttShell (NSH)
nsh> uname -a
NuttX 0.0.0 2f1387d5-dirty Aug 27 2026 10:15:32 risc-v esp32p4-function-ev-board

desktop_main  Waiting/Signal  stack 32704
/dev/fb0
/dev/input0
```

内存检查：

```text
Kmem total 489348, used 3724, free 485624
Umem total 33554428, used 2540036, free 31014392
```

结论：v1.0 专用启动兼容路径、LVGL 桌面任务、LCD framebuffer、GT911 输入节点、PSRAM 和后台 NSH 均已通过实机启动验收。实体触摸坐标和界面显示效果仍需操作者目视点击确认。

完整 UART 证据保存在仓库 `logs/v1-startup-fix-smoke-20260827.log` 和 `logs/v1-startup-fix-reset-cycle2-20260827.log`。
