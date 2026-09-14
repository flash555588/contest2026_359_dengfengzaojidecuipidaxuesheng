# OpenVela 移植状态

## 参赛仓边界

主参赛仓：`flash555588/contest2026_359_dengfengzaojidecuipidaxuesheng`

公共参考仓：

- `flash555588/esp32p4_nuttx`
- `flash555588/esp32p4_nuttx_apps`

按照大赛规则，板级和芯片适配代码归属主参赛仓；对 `nuttx` 与 `apps` 公共仓的修改必须在对应 fork 中维护，并通过公共仓 PR 提交到 `dev-ai-contest-2026`。主参赛仓只保存可复现的 overlay patch、构建脚本、固件证据和作品文档。

## 已验证基线

- ESP32-P4 revision v1.0，COM7，16 MiB Flash，32 MiB PSRAM。
- Simple Boot 应用偏移 `0x2000`，DIO 80 MHz。
- MIPI-DSI 1024x600 RGB565，双 framebuffer + VSync page flip。
- 桌面配置禁用 `LV_NUTTX_FBDEV_PARTIAL`，避免活动 scanout 页被逐块更新。
- LVGL 使用可缓存 PSRAM 绘制；`FBIO_UPDATE` 对当前脏区和上一同步区的行并集执行 cache write-back，并同步两页。
- 桌面模式跳过彩虹测试帧，启动时先完成两页预热，避免彩虹过渡、蓝屏闪烁和底部割裂线。
- QuickJS QPK 已支持 `text`、`button`、`number`、`panel`、`setText`、`setHidden`、`background`、`getSize`、`setColor` 和带执行预算的 `onSwipe`。

## 补丁来源

`tools/patches/0001-esp32p4-nuttx-overlay.patch` 来源于成员 `esp32p4_nuttx` 参考实现；`tools/patches/0002-esp32p4-apps-overlay.patch` 来源于成员 `esp32p4_nuttx_apps` 参考实现，并包含当前实板验证后的显示与 Quick-App 修复。

应用顺序：先在对应公共仓工作树应用 `0001`，再在对应 apps 工作树应用 `0002`；板级 overlay 由主参赛仓 manifest 映射。不要把两个 patch 应用到同一个仓库。

## 下一阶段

1. 参考成员 Quick-App runtime，继续移植图片、存储和输入对话框 API。
2. 将天气、2048、OuO 等内置 QPK 按功能拆分，逐个加入实板回归。
3. 在不阻塞 LVGL 主循环的前提下移植 Wi-Fi 异步连接和文件管理。
4. 补齐冷启动、触摸坐标、连续页面切换和长时间运行证据。
5. 更新主参赛仓 README、演示视频、作品说明和 `logs/flash555588/`，提交前运行仓库自检脚本。

## 规则依据

本状态按官方《大赛总览》《参赛代码提交指南》《AI Coding 日志归集与提交手册》和《新硬件适配赛道教程导航》整理。提交截止日期为 2026 年 9 月 20 日；作品还需提交 README、作品介绍文档、演示视频、专属仓地址和有效 AI Coding 日志。
