# ESP32-P4 Function-EV-Board openvela 新硬件适配

本项目面向 2026 首届 openvela AI 硬件开发者大赛“新硬件适配”赛道，目标是在乐鑫 ESP32-P4 Function-EV-Board v1.4 上完成 openvela/NuttX 启动、外部 PSRAM、MIPI-DSI LCD、触摸和图形桌面的板级适配。

当前已在 ESP32-P4 芯片 revision v1.0 实板上完成 Simple Boot、NuttShell、32 MB PSRAM 和 7 英寸 1024×600 MIPI-DSI 显示链路的 bring-up。LCD 已能稳定全屏输出 DSI Host 彩条测试图案；当前固件默认保留该诊断模式，用于隔离面板时序与帧缓冲 DMA 问题。

## 适配亮点

- 针对 ESP32-P4 revision v1.0 增加 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` 启动路径，并将 CPU 定档在实测稳定的 CPLL 90 MHz。
- 修复 Simple Boot 下看门狗、时钟、MSPI/XIP 初始化顺序问题，使 NSH 可稳定启动。
- 初始化 32 MB PSRAM，为 1024×600 RGB565 帧缓冲提供空间。
- 接入 Espressif MIPI-DSI Host、DPI Bridge、DPHY LDO 和 EK79007 类面板初始化流程。
- 按原厂 1024×600 配置校正 52 MHz DPI 像素时钟、900 Mbps 双 Lane 和水平/垂直消隐参数。
- 增加 DSI/DPI 寄存器、时钟、帧计数和中断状态诊断接口，支持测试图案与帧缓冲路径 A/B 定位。
- 保存可烧录固件、实板启动日志以及 AI Coding 会话日志，便于复现和审阅。

## 硬件环境

| 项目 | 配置 |
| --- | --- |
| 开发板 | ESP32-P4 Function-EV-Board v1.4 |
| 主芯片 | ESP32-P4 revision v1.0 |
| Flash | 16 MB，DIO，80 MHz |
| PSRAM | 32 MB |
| 屏幕 | 7 英寸 MIPI-DSI，1024×600，RGB565 |
| DSI | 2 Lane，900 Mbps，DPI 52 MHz |
| 串口 | CP2102N，UART0 GPIO37/38，115200 |
| LCD 控制 | RST GPIO27，背光 GPIO26 |

## 目录结构

```text
board/esp32p4-function-ev-board/  板级配置、启动、LCD、触摸与桌面代码
board/esp32p4-common/             ESP32-P4 公共板级支持 overlay
chip/esp32p4/                     ESP32-P4 架构和 Espressif 驱动 overlay
firmware/esp32p4-nsh/             可烧录固件与启动日志
logs/flash555588/                 AI Coding 日志和实板工作记录
tools/                             WSL 构建、同步、检查与 Windows 烧录工具
contest2026_*.xml                 repo manifest 与 linkfile 映射
```

`contest2026_359_dengfengzaojidecuipidaxuesheng.xml` 会把仓内板级与芯片 overlay 映射到 openvela 编译树，不需要直接修改工作区中的 `nuttx/` 或 `vendor/` 仓库。

## 获取工程

```bash
repo init -u https://github.com/open-vela/contest2026_359_dengfengzaojidecuipidaxuesheng \
  -b dev-ai-contest-2026 \
  -m contest2026_359_dengfengzaojidecuipidaxuesheng.xml
repo sync -c -j8
```

## 构建

当前构建流程在 WSL 中执行。仓内脚本会把 overlay 同步到 openvela 工作树并构建 ESP32-P4 NSH 配置：

```bash
python tools/wsl_build_p4_nsh.py
python tools/wsl_copy_firmware.py
```

底层构建入口为 `tools/wsl_make_p4_nsh.sh`。详细的启动、镜像生成和调试说明见 `board/esp32p4-function-ev-board/README.md` 与 `DISPLAY_PLAN.md`。

## 生成镜像与烧录

在 Windows 仓库根目录使用 esptool 生成 Simple Boot 镜像：

```powershell
python -m esptool --chip esp32p4 elf2image --ram-only-header `
  --flash_mode dio --flash_freq 80m --flash_size 16MB `
  -o firmware\esp32p4-nsh\nuttx.bin firmware\esp32p4-nsh\nuttx
```

按住 BOOT、点按 RST 进入下载模式后执行：

```powershell
powershell -File tools\flash_p4_nsh.ps1
```

脚本默认通过 COM7 以 460800 波特率将镜像烧录到 `0x2000`。如设备端口不同，请先修改脚本中的串口参数。

## 验证

串口使用 115200 波特率。成功启动后应看到：

```text
WARNING: NuttX supports ESP32-P4 chip revision > v3.0 (chip revision is v1.0).
Ignoring this error and continuing because CONFIG_ESP32P4_SELECTS_REV_LESS_V3 is set...
NuttShell (NSH)
nsh>
```

显示验证的当前判据是屏幕稳定覆盖全屏彩条。`esp32p4_display.c` 中的 `LCD_HOST_PATTERN_TEST` 默认设为 `1`，图案由 DSI Host 生成并绕过 PSRAM/DMA，可用于确认 DPHY、面板初始化和 DPI 时序链路。切换为帧缓冲模式前需继续验证 DW-GDMA 的连续整帧传输。

实板启动记录见 `firmware/esp32p4-nsh/bootlog.txt` 和 `logs/flash555588/`。

## AI Coding 使用说明

本项目使用 AI 辅助进行需求拆解、ESP32-P4 v1.0 勘误核对、Simple Boot 启动故障定位、时钟/XIP 实验设计、MIPI-DSI 驱动移植、DMA 与显示异常分析、代码审查和文档整理。完整的选定会话和工作记录位于 `logs/flash555588/`。

## 当前状态与后续工作

当前提交是可复现的阶段性基线：NSH、PSRAM 和 MIPI-DSI 全屏测试图案已通过实板验证。后续将完成 DW-GDMA 帧缓冲连续传输、GT911 触摸实测和 LVGL 桌面回归，并把适合上游的芯片层与板级改动拆分为独立 PR。
