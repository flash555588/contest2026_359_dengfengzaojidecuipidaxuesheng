# 静止画面与 Flash 操作（ESP32-P4 / EK79007）

2026-10-21 核对当前 Function-EV-Board 构建：EK79007、1024×600、RGB565、DSI 两条数据 lane，`MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST`。以下结论针对这套屏幕和现有 NuttX 驱动。

## 能否停止刷新并保持原画面

**暂停 GUI 绘制可以保留 framebuffer 内容，但不能停止显示像素流。当前公开接口没有可用的屏端整帧保持功能。**

依据官方开发板链接的 [EK79007AD Rev.1.9 手册](https://dl.espressif.com/dl/schematics/display_driver_chip_EK79007AD_datasheet.pdf)：

- 第 4 页方框图是解码、数据锁存、源极输出和时序控制，没有整帧 GRAM。
- 第 16 页 8.2.1 要求 Video Mode 实时提供像素流。Burst 指压缩一行的传送时间，在行间留出低功耗间隙，不表示可以停送任意时长。
- 第 21 页寄存器表没有写入整帧显存、设置像素窗口或开启自刷新的命令。第 23 页 Sleep In 是关电时序，不能用它保持图像。
- 第 26 页 `CABC_EN=10` 的 Still Picture 是背光控制类别；BIST 无需外部 DCLK，但产生测试图案，不能保存用户画面。不能把二者当成保帧开关。

驱动 `esp_mipi_dsi.c` 已在没有翻页时重复扫描同一 framebuffer；每帧完成后通过 `esp_mipi_dsi_dma_isr()` 调用 `esp_mipi_dsi_dma_restart()` 重新启动 DW-GDMA。仅停止 LVGL 无法消除这个中断依赖。`esp_mipi_dsi_video_stop()` 关闭 DMA 和 DPI 输出，是停流接口，没有屏端保帧保证。因此本次没有添加自动停流，也没有发送未知的厂商寄存器命令。

## 与蓝屏的关系

以前实体 A/A1/B1 对照支持：Flash 聊天历史保存是已找到的触发路径，停止相机和 USB 初始化仍会闪；去掉该轮 Flash 保存后，恢复相机也通过了用户观察。后来网页配置即使内容相同也触发约 55 次 Flash 写入和 7 次擦除，说明静止页面仍会受到存储操作影响。

[ESP-IDF 的 P4 Flash 并发说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/peripherals/spi_flash/spi_flash_concurrency.html)解释了默认缓存保护会暂停另一核并屏蔽非 IRAM 安全中断。当前 NuttX 移植保持该保护；它已有内部栈 Flash dispatcher，但将写操作放入独立线程并不能让显示中断在缓存关闭时安全运行。framebuffer 内容正确也不等于面板收到连续像素。

## 可行性比较

| 方案 | 对当前设备的判断 |
| --- | --- |
| 无脏区就不绘制，保留 framebuffer | 已有这类行为；降低绘制负载，但 DSI 仍须连续扫描，不能解决 Flash 中断间隙 |
| 暂停 DSI，屏端保存上一帧 | 当前 EK79007 无公开可用接口支持；不作为自动策略部署 |
| 保留旧图并连续扫描 | 正确目标，但必须让 DMA 重启链在 Flash 窗口仍能运行；先前 IRQ／自主 reload 试验实板失败，继续保持关闭 |
| 整帧移到内部 RAM | RGB565 一帧需 1,228,800 字节，当前内部 RAM 不足；双缓冲也不改变缓存关闭的问题 |
| XIP from PSRAM，Flash 写时保持 PSRAM/cache 可用 | 已增加显式 `--psram-xip` 的独立试验移植，正常构建默认关闭。包含启动复制、MMU、链接预留、PMP 顺序／两核回读及 SDK SPI1 cache-on mutex 路径。物理 Flash 写擦验收单独记录，不能仅凭开关或构建通过认定稳定 |
| 更换带 GRAM／明确支持自刷新的面板或外部帧缓冲桥 | 能从硬件上解耦扫描，需要换硬件和对应驱动 |

## 当前配置策略和验证边界

API Key、模型地址、语音和 Home Assistant/MCP 等基础配置始终以内置 Flash 为准，不依赖 SD。SD 配置双槽试验已撤回；没有迁移、删除旧配置或用户媒体。内容完全相同的配置保存跳过 Flash 写入，仍按原校验和密钥保留规则合并更新。该修正可消除“无变化却写 Flash”的触发，**不代表真正修改配置时的底层闪屏已经修好**。

宿主验证包括真实文件持久化、新进程读取、SD 存在／移除后仍读取最新 Flash 配置、无变化写入计数为零，以及保存失败和密钥脱敏。烧录校验、实际网页保存与实体屏幕观察分别记录在工作区 `output/display-hold-20261021/`；不能用构建通过或截图代替实体观察。

## 配置时暂停 AI

保存 AI、语音或 Agent/MCP 配置现在经过统一生命周期：阻止新对话、ASR、TTS／唤醒及端侧识别进入，等待已经接纳的启动发布所有权，取消当前工作，再等待实际网络／采集／播报资源退出。桌面线程负责清空 Bot 与唤醒组件的待发送、待播报状态；工作线程全部退出后再清理一次末尾事件，随后才保存配置。ASR 的 `active=false` 不是后台线程已经退出，必须检查真实 worker 状态。

等待上限 10 秒，超时不保存。保存成功或失败后重新读取当前配置，恢复服务待命和正常的后台唤醒；旧对话、工具、采集、上传和播报不会自动重放。历史与已完成录制仍保留。无变化保存继续跳过写入；基础配置仍在内部 Flash。

这项改动降低配置时 AI 的并发负载，并使配置替换的边界明确。它不会关闭 DSI 扫描，也没有绕过缓存保护，因此实体闪屏效果仍须独立验收。验证入口为 `tools/test_ai_config.py`，部署与实测记录在工作区 `output/ai-config-20261021/`。

## 应用草稿触发与 v5.5.6 文档核对

2026-10-21 用户报告“正在编写应用草稿”时两次必现闪屏。现场只读查询仍有响应、没有新 panic，但累计 Flash 写入/擦除已由此前普通 Bot 实测的 0/0 增至 166/14，最大擦除 55.630 ms，显示 IRQ 最大间隔 58.146 ms。这些是累计值，只能支持 Flash 停顿这一解释，不能冒充逐事件关联。

发现创作器 `glass_qpk_builder.c` 的 `.draft` 和按需 `.examples` 缓存仍固定内部 Flash，聊天历史的 SD-only 策略没有覆盖它们。已改为 builder 会话首次使用时优先选择挂载 SD，并固定该介质；选中后卡故障/拔卡/满盘不回落 Flash。新 SD 没有草稿时只读兼容内部旧稿，显式修改才提交 SD 新版本，原内部文件保留。已有 SD 墓碑或损坏不复活旧稿。无卡时保留原内部草稿功能及其 cache-off 显示限制；用户显式指定的正式应用安装介质不变。运行中后来插卡不自动切换既有草稿会话。

用户提供的 [ESP-IDF v5.5.6 P4 并发约束原文](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.5.6/esp32p4/api-reference/peripherals/spi_flash/spi_flash_concurrency.html)已从官方站点取回核对：默认 cache-off、非 IRAM 中断屏蔽与另一核暂停；IRAM 安全要求覆盖全部函数和数据。PSRAM XIP 将 `.text/.rodata` 启动时加载到 PSRAM，但 Flash mmap 区域仍存在例外；不能只把 framebuffer 放 PSRAM 就声称完成 XIP。Auto Suspend 依赖实际器件、MSPI 与时序，文档明确并非 LCD、蓝牙、Wi-Fi 等实时场景的通用解法。NuttX 当前未实现完整 PSRAM XIP 架构，已拒绝的自动挂起/IRQ/reload 实验继续关闭。

## PSRAM XIP 独立试验（2026-10-21）

前文“未实现”指此前已部署版本。现在 `tools/prepare_p4_psram_xip.py` 接通随工程 HAL 附带的 P4 `mmu_psram_flash_v2.c`，仅由 `tools/build_bilibili_v3.py --psram-xip` 显式启用；使用独立准备区，不修改基线，正常构建默认关闭。当前适用已核实的 P4 rev3.2、Simple Boot、32MiB PSRAM 和既有分区，不能当作所有 ESP 芯片的通用开关。

P4 的指令与常量都使用 `0x48000000` PSRAM 窗口。Simple Boot 解析实际镜像物理偏移，初期映射 Flash 别名，复制器将两段复制到独立的物理页；外部 BSS 和堆避开这些页。启动时逐页核对 MMU 目标，并通过独立 Flash 别名比对整个代码／常量内容的哈希。复制前的正常启动依赖及校准回调留在内部 RAM，不依靠叶子 ISR 的地址推断整个调用链安全。

原 Simple Boot／应用的 PMP 锁定必须推迟到复制结束；早期 APM 设置保留。指令边界按128字节对齐、常量保护末端向上对齐、堆保护采用实际映射末端，避免遗漏 MMU 页对齐间隙。CPU1 发布 ready 前也设置 PMP，两核回读 CSR 确认代码 RX、常量 R、堆 RW。SPI1 只有在复制和 CPU0 保护均验证成功后才允许采用 SDK 原有的 mutex/cache-on 路径，原内部栈 dispatcher 保留。没有重启已拒绝的自动挂起、cache-off IRQ 或 DSI reload。

试验记录位于工作区 `output/psram-xip-20261021/`：`ROLLBACK.json` 与 `rollback/nuttx.bin` 固化前一版回退镜像；`ELF-AUDIT.json`、`REVIEW.md`、`host-result.log`、`flash.log`、`boot.log` 和后续物理验收日志分别保留各阶段证据。初次实板启动已报告 `ready=1 protected_cpus=3`，USB摄像头仍枚举成功；这还不能代替真实 Flash 写擦时的屏幕观察。原始 Flash mmap 区域仍受官方并发限制，本试验没有承诺任意动态映射可以在写擦期间读取。

本轮物理验收已完成：小游戏8轮、USB预览4轮内部Flash自检均通过，累计3862次写入／387次擦除，最长擦除55.413ms，显示IRQ最大间隔16.612ms、欠载及翻页错误均0；用户两轮都确认“全程正常，无蓝屏或闪烁”。相机已停止并返回桌面，第6版草稿保留。结果只覆盖本轮有限试验，尚未做长期老化及断电冷启动；详见该目录 `VERIFICATION.md` 与 `RESULTS.json`。

构建与部署候选、宿主存储回归及实体测试分别保存在工作区 `output/draft-flash-20261021/`。草稿改存 SD 是移除可避免的触发路径，不能称为底层平台问题已解决。高噪声唤醒仍是独立问题；未通过召回/误触发对照的候选不得混入本次显示修复固件。
