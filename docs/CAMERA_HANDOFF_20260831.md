# ESP32-P4 v1.0 相机与频率交接记录

更新时间：2026-08-31

## 当前可交付基线

目标硬件为 ESP32-P4 revision v1.0 / ECO2，显示分辨率 1024×600，相机为 SC2336，两路数据通过 MIPI-DSI 与 MIPI-CSI 工作。

v1.0 的发布默认频率固定为 180 MHz。启动阶段先以 90 MHz 完成 32 MiB PSRAM 初始化，再切换到 180 MHz，随后把 PSRAM 加入堆。实机验证可进入 NuttShell，SC2336 首帧为 1,228,800 字节 RGB565，并成功注册 `/dev/video0`。

180 MHz 发布固件：`firmware/esp32p4-desktop-v1.0-release/nuttx-lvgl-unified-ui.bin`。

## 性能与内存

相机预览在 180 MHz 下约为 7.0–7.8 FPS，缩放耗时约 27.6–29.8 ms；90 MHz 基线约 4 FPS，缩放耗时约 50.9–54.6 ms。

冷启动占用约 2,558,736 字节；相机运行时约 5,735,736 字节；停止后约 2,578,808 字节。主要动态缓冲区共 3,072,000 字节，停止后残留差约 20 KiB，当前没有证据要求加入强制内存回收。

## 360 MHz 实机结果

360 MHz 仅保留为失败实验项，不得作为 v1.0 发布频率。诊断固件明确输出 `cpu freq: 360000000 Hz`，随后停在 `Adding pool of 32768K of PSRAM memory to heap allocator`，无法进入 NuttShell，也不会注册相机。额外早期日志会影响切换行为，说明该路径还存在明显的启动时序敏感性。

后续若继续验证 360 MHz，应优先使用 revision v3 硬件，或完整移植厂商针对 v1.0 的电压、时钟、计时和 PSRAM 配套初始化；不得只修改 Kconfig 默认值。

## 当前缺陷

1. v1.0 的 360 MHz 启动路径不完整，失败点位于 PSRAM 加入用户堆之后的早期启动阶段。
2. 软件缩放仍是相机预览的主要 CPU 开销；PPA 实验在首个事务处锁死，未纳入交付。
3. Simple Boot 镜像会出现 SHA-256 comparison failed 后继续启动的既有提示，发布验收应同时核对写入哈希与后续启动日志。
4. 当前 AI 日志收集器对新版 Codex `response_item` 事件格式兼容不足，需要在提交当前窗口时使用兼容转换并执行脱敏校验。

## 烧录与验收

```powershell
py -3.13 -m esptool --chip esp32p4 --port COM7 --baud 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 firmware/esp32p4-desktop-v1.0-release/nuttx-lvgl-unified-ui.bin
```

验收日志至少应包含：`cpu freq: 180000000 Hz`、`Adding pool of 32768K of PSRAM memory`、`Camera: first RGB565 frame OK`、`registered at /dev/video0` 和 `NuttShell (NSH)`。
