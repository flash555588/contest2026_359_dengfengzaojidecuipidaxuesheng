# Glass Claw OpenVela v3 恢复候选

这是 2026-09-13 从保留的 v3 基线恢复、补齐当前桌面/C6 补丁并重新完整链接的
ESP32-P4 Function-EV OpenVela 离线候选。OpenVela 使用 NuttX 内核及其原生
`nuttx/` + `apps/` 源码布局，因此最终 ELF/BIN 沿用 `nuttx` 文件名；这不是把
系统替换成了另一个 RTOS。

最终配置启用了 400 MHz、双核 SMP、1024×600 RGB565 桌面、SC2336 +
MIPI-CSI 相机链、ESP-Claw、C6 SDIO/DMA 网络适配，以及
`CONFIG_SYSTEM_C6_DESKTOP=y`。C6 桌面同步包含 worker/backend、扫描结果、链路
快照、RPC 邮箱、RPC 串行化和 Hosted 轮询所有权保护。原先的单体 RAW10 相机包
没有覆盖当前模块化相机驱动。

`nuttx.bin` 大小为 3,816,784 字节，烧录偏移是 `0x2000`，距 `/data` 起点
`0x400000` 仍有 369,328 字节。镜像面向 ESP32-P4 v3.1–v3.99、16 MB Flash、
DIO/80 MHz，校验和有效。BIN SHA-256：
`5b3432c90032126751cdc1452bc18613fa0e707bd2911902c656877ac37e17af`。

已完成目标全编译/最终链接、构建日志失败标记检查、最终配置与 ELF 关键符号检查、
C6 协议/并发/SDIO/桌面后端宿主测试、JavaScript 桥接 9 项测试，以及带 C6 Wi-Fi
编译路径的 LVGL 离屏交互 2 项测试。构建日志保留 6 条既有警告，但没有 fatal、
递归 make 错误或未初始化 preferences 警告。

该包仍不是硬件验收版：没有刷机，没有执行 C6 射频命令、真实 Wi-Fi 关联/DHCP、
显示触摸、SC2336 传感器或在线 ESP-Claw/TLS 验收。BLE 未进入 v3；现有 Hosted
BLE 传输源码明确限制在 pre-v3 芯片，不能据此宣称 v3 蓝牙可用。不要整片擦除、
移动或格式化 `/data`。

`sync-evidence/synced-sources/` 保存此次进入隔离 v3 树的关键源码快照；
`C6-COMPOSITION-FINAL.json`、`C6-INTEGRATION-INITIAL.json`、补丁和工具保存同步
来源与恢复证据。原始 v3 目录和旧 `glass-claw-v3-candidate.zip` 均未覆盖。

本机复现命令使用项目中的 `espclaw-port/tools/build_joint.py`，对隔离工作区指定
`v3 --resume --c6-desktop`。脚本会解析配置、重建匹配配置的 TLS 静态库、记录并
检查完整 make 日志、验证相机/C6/ESP-Claw 最终符号，并拒绝越过 `0x400000` 的镜像。

