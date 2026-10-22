---
name: p4-camera-display
description: 排查 openvela/NuttX ESP32-P4 的 CSI/UVC 预览撕裂、蓝屏、闪屏、DMA 缓存与 DSI 翻页问题，以及 Flash 写擦和显示中断的关联。适用于相机显示交接或 P4 显示故障，不用于一般 ESP-IDF 相机工程。
---

# P4 相机、显示与 Flash 故障定位

先定位含 `tools/build_bilibili_v3.py` 的项目根，读取 `AGENTS.md`，确认芯片 revision、屏幕、摄像头类型、实际端口和当前固件。USB Serial/JTAG 与 OTG HS 不同；GPIO26/27 被显示占用时不能启用 FS。

## 建立可归因的证据

区分整屏黑/蓝、预览窗口异常、撕裂、静态截图正常但实体闪烁。按当前触发步骤复现，关联帧计数、scanout offset、显示 IRQ 最大间隔、Flash 写入/擦除计数和首故障寄存器。ISR 只记录 IRAM 安全计时与计数，在线程中整理日志。

历史上禁用相机/USB 后，Bot 写 Flash 历史仍会闪屏。不要把最近新增的外设直接当作根因；在独立候选中一次改变一个因素，记录是否真的发生内部 Flash 写入/擦除。路径和失败实验边界见 [references/diagnosis.md](references/diagnosis.md)。

## 证明缓冲区所有权

画出采集、解码/处理、待显示、正在扫描、可复用状态及转换条件。刚交给显示的 USERPTR 页不能立即 `QBUF` 给 CSI。停止并 join 后，将最后画面安全停靠到第三页，确认实际 scanout，再归还 GUI 绘制所有权。

CSI `DQBUF` 后核对显式 M2C；CPU 绘制后核对 C2M。P4 HAL 管理缓存时，通用失效 API 可能为空操作。保留 DMA 对齐和缓存要求；CPU 元数据、内核堆、PSRAM 用户堆分别核算。

启动硬件须在整次 LVGL handler 返回后。翻页失败时停止流，不能复用仍可能被扫描的页。待重绘状态也保持显示暂停，但不要误判为仍有录像源。

## Flash/cache 修改的边界

保留已拒绝的 cache-off IRQ、Flash 自动挂起和 DSI reload 实验的禁用状态。叶子函数宿主测试或 cache-on probe 不足以推翻实板失败结果。

用户要求底层修复时，继续推进完整机制验证，不能把内容改存 SD 称为根治。PSRAM XIP 需在独立准备区显式启用，证明启动复制闭包、真实镜像偏移、BSS/堆预留和两核保护；不能只开配置或删除 cache-off 保护。

## 验收

相机所有权/缓存修改选择 `test_camera_boundary.py`、`test_camera_sync.py`；插桩选择 `test_camera_timing.py`；USB 路径另选对应 USB 测试。具体参数先读脚本。

分别报告宿主状态机、最终 ELF/构建、烧录读回、冷启动、真实预览与实体屏幕观察。XIP 验收须有真实内部 Flash 写擦和同期屏幕观察，SD 写入与静态截图不能替代。保持回退镜像和用户数据，限定本轮负载与时长的结论。
