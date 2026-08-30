# Vela Mood Console

Vela Mood Console 是为 OpenVela ESP32-P4 Quick-App runtime 编写的原创状态表情应用。项目参考了通用的“配置驱动表情引擎”和“AI 消息切换状态”产品思路，但没有使用、改写或分发 `aora-bot` 的源码、SVG、图片、角色造型、配色、眼环数据或动画参数。

## 原创设计

角色采用嵌入式状态终端造型：外框、状态屏、双纯色胶囊眼、扫描条和底部状态槽均由 OpenVela Quick-App UI 原语实时绘制。应用不包含图片资源，也不依赖浏览器 SVG。

## 能力

- 32 个稳定状态 ID：`00-09` 生命周期、`10-19` 情绪、`30-41` Agent 工作状态。
- 配置驱动的眼睛、光标、状态槽、颜色、眨眼和运动参数。
- `setEmotion(id)`、`setGaze(x, y)`、`handleAIMessage(message)`。
- 未知 ID、无效 JSON 或缺失字段自动回退 `02`，不会留下空白页面。
- 自动巡演、滑动切换、待机呼吸、眨眼、扫描和轻量抖动。
- 当前状态使用 `system.storage` 保存；当前固件为 TMPFS，重启后清空。
- 零图片、零第三方代码、Apache-2.0。

## 文件

- `app.js`：唯一应用源文件。
- `manifest.json`：Quick-App 元数据。
- `DESIGN.md`：状态协议与原创设计说明。

固件中的内置 C 字符串必须由 `app.js` 机械生成，不应手工维护第二份脚本。
