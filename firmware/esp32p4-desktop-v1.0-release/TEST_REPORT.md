# ESP32-P4 v1.0 实机验收报告

测试时间：2026-08-31（Asia/Shanghai）；目标板：ESP32-P4 Function EV Board；芯片：ESP32-P4 revision v1.0 / ECO2；MAC：`60:55:f9:fa:f4:8b`；串口：Silicon Labs CP210x，COM7，UART0 115200 8N1。

当前统一桌面与相机固件 `nuttx-lvgl-unified-ui.bin` 大小为 3150160 bytes，SHA-256 为 `54b94cf0aacf95a0e0adc41214274e3186cb40ed2a85bf30251f773540c92560`。镜像写入偏移 `0x2000`，esptool 5.3.1 完成压缩写入、写后校验和硬复位。

180 MHz 固件进入 NuttShell，未出现 `CHIP_LP_WDT_RESET`、panic 或异常。启动关键证据为：

```text
cpu freq: 180000000 Hz
esp_psram: Adding pool of 32768K of PSRAM memory to heap allocator
Camera: first RGB565 frame OK, 1228800 bytes
Camera: SC2336 1024x600 RGB565 via ISP registered at /dev/video0
NuttShell (NSH)
```

运行态检查结果：

```text
desktop_main  Waiting/Signal  stack 32704
/dev/fb0
/dev/input0
Kmem total 489332, used 3724, free 485608
Umem total 33554428, used 2540036, free 31014392
```

结论：v1.0 的 180 MHz 专用启动兼容路径、桌面、LCD framebuffer、GT911、PSRAM、SC2336 相机和后台 NSH 已通过实机启动验收。360 MHz 诊断固件可以打印真实 360 MHz，但随后停在 PSRAM 加入堆阶段，未进入 NSH，禁止作为发布固件。

完整 UART 证据位于 `logs/v1-repro2-smoke-20260827.log` 和 `logs/v1-repro2-reset-cycle2-20260827.log`；对应构建日志为 `logs/build-repro-v1.0-20260827.log`。
