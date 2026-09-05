# ESP32-P4 Function-EV-Board openvela 新硬件适配

本项目面向 2026 首届 openvela AI 硬件开发者大赛“新硬件适配”赛道，目标是在乐鑫 ESP32-P4 Function-EV-Board 上完成 openvela/NuttX 启动、外部 PSRAM、MIPI-DSI LCD、GT911 触摸和 LVGL 图形桌面的板级适配。仓库同时维护早期 revision v1.x 与量产 revision v3.2 两套可复现配置。

当前已在 ESP32-P4 revision v1.0 实板上完成 Simple Boot、稳定 NuttShell、32 MiB PSRAM 系统堆、MIPI-DSI/DW-GDMA framebuffer、`/dev/fb0`、GT911 `/dev/input0`、LVGL 桌面与 QuickJS 快应用运行时 bring-up。串口与实物已验证完整 1024×600 首帧更新、连续 DMA 帧计数、全屏 LVGL 桌面、触摸设备打开及 **GT911 实际点击/拖动坐标**（LVGL 输出 `TOUCH: pressed x=... y=...`，坐标方向正确）。2026-08-30 的最终 v1.0 固件还通过了重新烧录、镜像 SHA-256 校验和真实断电冷启动，OuO/QuickJS 自动启动并进入 NSH。剩余验收项为 v3.2 芯片实板回归；v3.2 配置已完成构建，但不得烧入 v1.0 芯片。

## 适配亮点

- 针对 ESP32-P4 revision v1.0 增加 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` 启动路径，并将 CPU 定档在实测稳定的 CPLL 90 MHz。
- 修复 Simple Boot 下看门狗、时钟、MSPI/XIP 初始化顺序问题，使 NSH 可稳定启动。
- 将 32 MiB PSRAM 纳入系统堆；v1.x 使用稳定的 80 MHz，v3.2 使用原厂 200 MHz。
- 接入 Espressif MIPI-DSI Host、DPI Bridge、DPHY LDO 和 EK79007 类面板初始化流程。
- 按 Espressif EK79007 参考配置校正双 Lane 900 Mbps 与完整消隐参数；v1.x 使用已实测全屏的 24 MHz 低带宽档，v3.2 使用 52 MHz 原厂档。
- 接入 `/dev/fb0`、LVGL 9 NuttX framebuffer 后端与 GT911 轮询触摸，并按脏行执行 PSRAM cache writeback。
- 为双缓冲 page-flip 的生产者与 ISR 回调增加 SMP 自旋锁，避免待提交 framebuffer 指针竞争；同时把不适合在 ISR 中执行的 framebuffer upper-half 访问移到任务上下文。
- GT911 使用官方 BSP 的 HP I2C1（SCL=GPIO8 / SDA=GPIO7，0x5D 或 0x14 地址探测）；触摸初始化必须放在 MIPI-DSI 显示初始化之后，否则首个面板 DCS 写会以 `-110` 超时、整屏无法点亮。
- 扩展 QuickJS QPK 运行时，提供 OuO、2048、持久化 `system.storage` 与手势交互；补齐路径校验、存储命名隔离、LVGL 分配失败清理和回调生命周期管理。
- 为 LVGL NuttX framebuffer 增加可配置渲染缓冲模式，并为 tiny-ttf 字形缓存补充 OOM 防护。
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
| DSI | 2 Lane，900 Mbps；v1.x DPI 24 MHz / v3.2 DPI 52 MHz |
| 串口 | CP2102N，UART0 GPIO37/38，115200 |
| LCD 控制 | RST GPIO27，背光 GPIO26 |

## 目录结构

```text
board/esp32p4-function-ev-board/  板级配置、启动、LCD、触摸与桌面代码
board/esp32p4-common/             ESP32-P4 公共板级支持 overlay
chip/esp32p4/                     ESP32-P4 架构和 Espressif 驱动 overlay
firmware/esp32p4-nsh/             可烧录固件与启动日志
firmware/esp32p4-desktop-v1/      2026-08-30 最终 v1.0 桌面固件与测试报告
firmware/esp32p4-sc2336-camera-v1.0/  2026-08-30 SC2336 RAW8 取帧固件与测试报告
firmware/esp32p4-camera-preview-v1.0/ 2026-09-03 SC2336 RGB565 零拷贝实时预览固件与测试报告
ouo/                              OuO QuickJS 应用源码、清单与设计说明
logs/flash555588/                 AI Coding 日志和实板工作记录
tools/patches/                     最终 nuttx/apps 可复现补丁
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

最终 v1.0 桌面/OuO/相机预览版本需要在 `repo sync` 后按顺序应用可复现补丁（nuttx：0001、0003、0004、0006、0007、0008、0011、0013、0014、0015、0016、0018、0020、0022、0024、0026；apps：0002、0005、0009、0010、0012、0017、0019、0021、0023、0025、0027）：

```bash
cd contest2026_359_dengfengzaojidecuipidaxuesheng
bash tools/apply_final_overlays.sh

cd ../nuttx
tools/configure.sh esp32p4-function-ev-board:desktop-v1
make CROSSDEV=/path/to/riscv32-esp-elf/bin/riscv32-esp-elf- -j16
```

补丁固定基线为 nuttx `2f1387d56eb04ad2599baca58a3fa2380cdaaedb` 与 apps `88827afd368d4bbb4802b96ed44d9582f85b2f92`。应用脚本会校验基线、补丁 SHA256、工作树清洁性和应用后文件摘要；二次执行只在补丁集与结果都未变时安全跳过。桌面、OuO 和相机预览均由 `apps/system/desktop` 提供；board 层只负责注册 framebuffer、触摸和相机设备，不再包含或自动启动 LVGL 应用代码。0008/0009 还提供 `/dev/dsi-diag0` 与 `dsi_diag`，用原子 ioctl 快照取代依赖链接地址的 DSI 计数读取。0012 修复 QuickJS 归档文件名不一致，校验固定归档 SHA256，并使 LVGL Kconfig 版本与已固定的 v9.2.1 源码一致。0013 将 `esp-hal-3rdparty` 固定到 `78c092909fca38d1e2ccf767b5eff66bddc5c789`，并把原先的临时 Python 改写收敛为可审查、可重复应用的 HAL patch。0014 恢复 DSI 同步换页 API，并保留 LPWORK 回调模式，修正 0008 引入的头文件/实现不一致。0015 在生成的 sensor/panel 源码中记录固定的 Espressif 来源 commit；0016/0017 收口已确认的 NuttX 和应用头文件 nxstyle 缺陷；0018 使 CMake 与 Make 使用同一 HAL revision，并在 CMake 配置阶段核对 revision、幂等应用芯片级 HAL patch；0019 对齐 QuickJS 的 Make/CMake 源文件、RISC-V 浮点环境常量、补丁和下载校验；0020/0021 补齐 CSI/HAL、board camera 与 desktop 应用的 CMake target，0022 补齐 CSI HAL 的 CMake include path。完整许可清单见 `THIRD_PARTY.md`。

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

默认固件使用 PSRAM RGB565 framebuffer，而非 DSI Host 内部测试图案。启动后先保留全屏色条 3 秒，再切换到浅色 LVGL 桌面；串口应出现 `DESKTOP: touch ready`、首次 `DISP: update ... w=1024 h=600`，且 `ps` 中同时存在 `nsh_main` 与 `desktop`。点击屏幕后应出现 `TOUCH: pressed x=... y=...`。主页点击 `touch test` 可进入触控测试页：该页显示按下/拖动/释放计数、实时坐标、6 个分区命中和最近释放坐标；串口同步输出 `TOUCHTEST: down ...` 与 `TOUCHTEST: up ...`。

v1.x 回归命令示例：

```powershell
python tools\p4_serial_smoke.py --port COM7 --seconds 16 `
  --command "cat /proc/uptime" --command free
```

每次复位后应看到 `PSRAM: size=33554432 initialized=1`、
`TOUCH: gt911 /dev/input0 -> 0`、`DISP: display_init -> 0`、
`NuttShell (NSH)` 和 `DESKTOP: ui ready`。2026-08-23 已在同一镜像上连续执行
3 轮并通过；日志保存在 `logs/v1-stability-*.log`。

最终固件、哈希和 2026-08-30 烧录/冷启动记录见 `firmware/esp32p4-desktop-v1/`；早期实板启动记录见 `firmware/esp32p4-nsh/bootlog.txt` 和 `logs/flash555588/`。

## AI Coding 使用说明

本项目使用 AI 辅助进行需求拆解、ESP32-P4 v1.0 勘误核对、Simple Boot 启动故障定位、时钟/XIP 实验设计、MIPI-DSI 驱动移植、DMA 与显示异常分析、代码审查和文档整理。完整的选定会话和工作记录位于 `logs/flash555588/`。

## 当前状态与后续工作

当前提交是可复现的阶段性基线：双 revision 构建通过；v1.0 实板已验证稳定 NSH、32 MiB PSRAM、DW-GDMA 连续整帧、24 MHz 全屏 LVGL framebuffer 更新、GT911 实际点击/拖动坐标、OuO/QuickJS 自动启动，并通过连续复位与真实断电冷启动回归。剩余验收项为 v3.2 芯片上的对应实板回归。
