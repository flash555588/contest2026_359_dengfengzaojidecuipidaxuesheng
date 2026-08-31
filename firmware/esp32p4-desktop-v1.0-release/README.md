# ESP32-P4 v1.0 OpenVela 桌面发行包

这是 ESP32-P4 Function EV Board、芯片 revision v1.0 / ECO2 的专用 OpenVela/NuttX 桌面与相机固件。2026-08-31 已在本机 COM7 完成写后校验和硬复位启动验证。

本包默认 CPU 频率为 180 MHz。启动时先使用 90 MHz 完成 32 MiB PSRAM 初始化，再切换到 180 MHz；360 MHz 在 v1.0 实机上会停在 PSRAM 加入堆之后，因此仅记录为失败实验项。

启动后 LCD 运行 `desktop_main`，UART0 以 115200 8N1 提供后台 NSH。串口验收确认 `/dev/fb0`、GT911 `/dev/input0`、32 MiB PSRAM 用户堆和桌面任务均可用。实体屏幕颜色、方向和触摸四角仍需操作者目视完成最终验收。

不要把本包烧录到 v3.x 芯片；v3.2 使用独立配置和候选固件包。

## 烧录

在 Windows PowerShell 中执行：

```powershell
.\flash.ps1 -Port COM7
```

等价命令：

```powershell
py -m esptool --chip esp32p4 --port COM7 --baud 921600 `
  --before default-reset --after hard-reset write-flash `
  --flash-size 16MB --flash-mode dio --flash-freq 80m `
  0x2000 .\nuttx-lvgl-unified-ui.bin
```

## 固件校验

```text
nuttx-lvgl-unified-ui.bin  3150160 bytes  54B94CF0AACF95A0E0ADC41214274E3186CB40ED2A85BF30251F773540C92560
```

`nuttx.bin` 保留为 2026-08-27 的旧桌面基线，不再由 `flash.ps1` 默认烧录。当前交付件是包含 SC2336、MIPI-CSI 和统一 LVGL 桌面的 `nuttx-lvgl-unified-ui.bin`。

## 可复现来源

```text
NuttX          cd61ccdd11498e22c058c3b2540828f88e23172e
apps           93fb5ac72249ae766cbeea9f0e3d484bdd6807f7
LVGL           59a6b61c9580b65089010c5273f2fcdd6c4d2aae
QuickJS source 6e2e68fd0896957f92eb6c242a2e048c1ef3cae0
ESP HAL        8d0a898910084206721a0892ab093021bca1496a
mbedTLS        582ff482038db6e4010dbf6f943d97b05ad06ea5
toolchain      riscv32-esp-elf GCC 15.2.0_20251204
configuration  esp32p4-function-ev-board:desktop-v1
NuttX overlay   tools/patches/0005-espclaw-nuttx-camera-lvgl-network.patch
apps overlay    tools/patches/0004-espclaw-apps-camera-homeassistant-network.patch
```

成员 NuttX/apps 仓当前按团队策略保持私有。内部复现使用固定 SHA 和只读本地镜像；比赛最终交付前仍需选择公开这些固定提交，或将完整源码快照纳入评委可访问的交付仓。
