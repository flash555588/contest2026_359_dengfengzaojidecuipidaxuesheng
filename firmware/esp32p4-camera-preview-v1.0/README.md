# ESP32-P4 v1.0 SC2336 相机 RGB565 实时预览版

本目录是 ESP32-P4 Function EV Board v1.0 的相机实时预览交付物，在 `esp32p4-sc2336-camera-v1.0`（RAW8 取帧）的基础上完成了颜色、显示带宽和缓冲所有权三方面的修复：

- ISP 输出 RGB565，并启用 demosaic、CCM（v1.0 芯片没有白平衡增益模块，用 CCM 对角 `1.70/0.95/1.55` 代替，消除偏绿）和 color 三段；
- CSI DMA 直接写入 V4L2 USERPTR 显示页（零拷贝），删除中间 PSRAM 缓冲和 DW-GDMA 二次拷贝；MIPI-DSI 每帧的 FIFO underrun 从约 33 次/秒降到 0，蓝屏闪烁消失；
- 内核堆从 32 KB 提高到 128 KB，DSI/CSI DMA 描述符与 EMAC 缓冲不再争抢同一个池子，`eth0` 正常注册；
- 三页 framebuffer 下 LVGL 仍保留双缓冲（`lv_nuttx_fbdev.c` 页数判断放宽为 `>=`）；
- DSI 驱动新增 `g_esp_mipi_dsi_frames` / `g_esp_mipi_dsi_underruns` 计数器，可用 NSH `xd` 直接读取，作为显示质量的客观证据。

烧录：

```powershell
py -3.13 -m esptool --chip esp32p4 --port COM7 --baud 921600 write-flash --flash-mode dio --flash-freq 80m --flash-size 16MB 0x2000 nuttx.bin
```

启动后从 NSH 控制预览：

```text
desktop camera        # 启动相机预览，串口每 300 帧输出 [qpk] camera health
desktop camera-stop   # 停止预览并回到桌面
xd <addr> 8           # 读取 DSI 帧数 / underrun 计数（地址见 TEST_REPORT.md）
```

代码补丁为 `tools/patches/0004-esp32p4-camera-rgb565-preview.patch`（nuttx，接在 0001、0003 之后）和 `tools/patches/0005-esp32p4-desktop-camera-preview.patch`（apps，接在 0002 之后），`bash tools/apply_final_overlays.sh` 会按顺序应用。基线、编译器和烧录参数见 `BUILD-METADATA.txt`，实机证据见 `TEST_REPORT.md` 与两份 acceptance 日志。

注意：本固件针对 revision v1.0/ECO2 样板，启用了低于 rev3 的实验性兼容选项，不应用于量产。CPU 固定 180 MHz；360 MHz 未在本版本中验证。
