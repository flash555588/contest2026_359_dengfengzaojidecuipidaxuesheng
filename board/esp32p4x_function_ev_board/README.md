# ESP32-P4X-Function-EV-Board（文档名）

> 实测板子是 **ESP32-P4 rev v1.0 + CP2102N**，不是 P4X v3.1。板级代码已迁到 [`../esp32p4-function-ev-board/`](../esp32p4-function-ev-board/README.md)。本文件只保留早期硬件笔记。

# ESP32-P4X-Function-EV-Board

大赛「新硬件适配」待适配板。主芯片为 ESP32-P4 **rev v3.1+**（与旧款 ESP32-P4-Function-EV-Board v1.5.2 的差别是硅版本，不是另一套外设）。

官方用户指南：<https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html>

NuttX 已有相近板级 `esp32p4-function-ev-board`，openvela 尚未适配。板级代码放本目录，比赛期间由 manifest 映射进编译树；获奖后再 PR 到 `vendor_espressif` 的 `dev-ai-contest-2026`。

## 硬件要点

- 双核 RISC-V（最高约 400 MHz）、板载 16 MB SPI flash、最大 32 MB PSRAM。
- Wi-Fi 6 / BLE 走板载 **ESP32-C6-MINI-1**，P4 本身没有无线。
- 烧录请用标了 **USB Serial/JTAG** 的 Type-C（芯片内置 CDC，Windows 上是 Espressif VID `303A` / PID `1001`）。不要用 USB 2.0 High-Speed 或 Full-speed 口烧录。
- 电源拨到 ON。线必须是数据线。
- P4X v3.1 **不要开安全下载模式**（勘误 ROM-770）。
- LCD：MIPI DSI；复位默认 GPIO27，背光 PWM 默认 GPIO26。摄像头走 MIPI CSI。

## 本机 ESP-IDF 环境（已装）

Windows 上已有 ESP-IDF **v6.0.2**，目标含 `esp32s3` 与 `esp32p4`。P4X 默认 `CONFIG_ESP32P4_REV_MIN_301=y`。

PowerShell：

```powershell
. C:\Users\flash\esp\activate-p4.ps1
cd C:\Users\flash\esp\hello_world_p4
idf build
idf flash
idf monitor
```

Git Bash：

```bash
source ~/esp/activate.sh
cd ~/esp/hello_world_p4
idf set-target esp32p4   # 仅首次
idf build
idf flash
idf monitor
```

识别芯片（端口号以设备管理器为准）：

```powershell
python -m esptool --chip esp32p4 --port COMx chip-id
```

进入下载模式：按住 BOOT，点一下 RST，先松 RST 再松 BOOT。USB Serial/JTAG 多数情况会自动复位进下载，不必每次手动。

若电脑只枚举出 **CP2102N**（VID `10C4` PID `EA60`），那是旧款 v1.4 的 USB-UART，或外接转串口，不是 P4X 的 USB Serial/JTAG。P4X 应出现 Espressif `303A:1001`。Windows 上 CP2102N 若驱动安装失败、没有 COMx，可在管理员 PowerShell 执行：

```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl Ubuntu-22.04 --busid <BUSID>
```

然后在 WSL 里用 `/dev/ttyUSB0` 烧录。
