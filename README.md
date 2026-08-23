# ESP32-P4 Function-EV-Board openvela 新硬件适配

本项目面向 2026 首届 openvela AI 硬件开发者大赛“新硬件适配”赛道，目标是在乐鑫 ESP32-P4 Function-EV-Board 上完成 openvela/NuttX 启动、外部 PSRAM、MIPI-DSI LCD、GT911 触摸和 LVGL 图形桌面的板级适配。仓库同时维护早期 revision v1.x 与量产 revision v3.2 两套可复现配置。

当前已在 ESP32-P4 revision v1.0 实板上完成 Simple Boot、稳定 NuttShell、32 MiB PSRAM 系统堆、MIPI-DSI/DW-GDMA framebuffer、`/dev/fb0`、GT911 `/dev/input0` 与 LVGL 桌面 bring-up。串口与实物已验证完整 1024×600 首帧更新、连续 DMA 帧计数、全屏 LVGL 桌面、触摸设备打开及 GUI 运行后的 `ps/free`；冷上电初始化与实际点击坐标仍在回归。v3.2 配置已完成构建，但不得烧入 v1.0 芯片。

## 适配亮点

- 针对 ESP32-P4 revision v1.0 增加 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` 启动路径，并将 CPU 定档在实测稳定的 CPLL 90 MHz。
- 修复 Simple Boot 下看门狗、时钟、MSPI/XIP 初始化顺序问题，使 NSH 可稳定启动。
- 将 32 MiB PSRAM 纳入系统堆；v1.x 使用稳定的 80 MHz，v3.2 使用原厂 200 MHz。
- 接入 Espressif MIPI-DSI Host、DPI Bridge、DPHY LDO 和 EK79007 类面板初始化流程。
- 按原厂 1024×600 配置校正双 Lane 1000 Mbps 与完整消隐参数；v1.x 使用已实测全屏的 24 MHz 低带宽档，v3.2 使用 48 MHz 原厂档。
- 接入 `/dev/fb0`、LVGL 9 NuttX framebuffer 后端与 GT911 轮询触摸，并按脏行执行 PSRAM cache writeback。
- 增加 DSI/DPI 寄存器、时钟、帧计数和中断状态诊断接口，支持测试图案与帧缓冲路径 A/B 定位。
- 保存可烧录固件、实板启动日志以及 AI Coding 会话日志，便于复现和审阅。

## 硬件环境

| 项目 | 配置 |
| --- | --- |
| 开发板 | ESP32-P4 Function-EV-Board（实测板 v1.4） |
| 主芯片 | ESP32-P4 revision v1.0；另提供 v3.2 构建配置 |
| Flash | 16 MB，DIO，80 MHz |
| PSRAM | 32 MiB；v1.x 80 MHz / v3.2 200 MHz |
| 屏幕 | 7 英寸 MIPI-DSI，1024×600，RGB565 |
| DSI | 2 Lane，1000 Mbps；v1.x DPI 24 MHz / v3.2 DPI 48 MHz |
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
# revision v1.x
python tools/wsl_build_p4_nsh.py nsh
python tools/wsl_copy_firmware.py --variant v1.x

# revision v3.2
python tools/wsl_build_p4_nsh.py nsh-v3
python tools/wsl_copy_firmware.py --variant v3.2
```

底层构建入口为 `tools/wsl_make_p4_nsh.sh`。详细的启动、镜像生成和调试说明见 `board/esp32p4-function-ev-board/README.md` 与 `DISPLAY_PLAN.md`。

## 生成镜像与烧录

构建脚本使用工作树固定的 esptool 生成带 `--ram-only-header` 的 Simple Boot 镜像，并由归档脚本原样复制；不要再用其他 esptool 版本重复转换 ELF。

按住 BOOT、点按 RST 进入下载模式后执行：

```powershell
# 先用 esptool chip-id 或启动日志确认 revision，再选择对应镜像
powershell -File tools\flash_p4_nsh.ps1 -Variant v1.x -Port COM7
powershell -File tools\flash_p4_nsh.ps1 -Variant v3.2 -Port COM7
```

脚本默认以 460800 波特率将镜像烧录到 `0x2000`。ESP32-P4 Simple Boot 镜像不能写到 `0x0`；v3.2 镜像也不能用于本仓当前实测的 v1.0 芯片。

## 验证

串口使用 115200 波特率。成功启动后应看到：

```text
WARNING: NuttX supports ESP32-P4 chip revision > v3.0 (chip revision is v1.0).
Ignoring this error and continuing because CONFIG_ESP32P4_SELECTS_REV_LESS_V3 is set...
NuttShell (NSH)
nsh>
```

默认固件使用 PSRAM RGB565 framebuffer，而非 DSI Host 内部测试图案。启动后先保留全屏色条 3 秒，再切换到浅色 LVGL 桌面；串口应出现 `DESKTOP: touch ready`、首次 `DISP: update ... w=1024 h=600`，且 `ps` 中同时存在 `nsh_main` 与 `desktop`。点击屏幕后应出现 `TOUCH: pressed x=... y=...`。

实板启动记录见 `firmware/esp32p4-nsh/bootlog.txt` 和 `logs/flash555588/`。

## AI Coding 使用说明

本项目使用 AI 辅助进行需求拆解、ESP32-P4 v1.0 勘误核对、Simple Boot 启动故障定位、时钟/XIP 实验设计、MIPI-DSI 驱动移植、DMA 与显示异常分析、代码审查和文档整理。完整的选定会话和工作记录位于 `logs/flash555588/`。

## 当前状态与后续工作

当前提交是可复现的阶段性基线：双 revision 构建通过；v1.0 实板已验证稳定 NSH、32 MiB PSRAM、DW-GDMA 连续整帧、24 MHz 全屏 LVGL framebuffer 更新和 GT911 设备打开。剩余验收项是冷上电初始化回归、GT911 实际点击坐标/方向，以及 v3.2 芯片上的对应实板回归。
