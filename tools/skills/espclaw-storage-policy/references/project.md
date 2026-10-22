# 当前数据分类与测试

所有路径相对项目根；若代码/文档有冲突，按适用指令和本次用户要求核对，不能照搬较早试验结论。

| 数据 | 当前策略 | 关键失败语义 |
| --- | --- | --- |
| AI/语音/HA/MCP 基础配置 | 内置 Flash | 同值跳过写入；不能转存 SD 掩盖闪屏 |
| 新 Bot/语音/工作台历史、长答案 | SD；无卡内存会话 | 不回退 Flash；旧 Flash 历史保留 |
| 原始采样/观察文字 | `espclaw-recordings` / `espclaw-observations`（SD） | 不自动重录上传，不迁移旧内容 |
| 相机照片 | SD 已挂载时优先 SD | 保存前检查余量；故障不回退 Flash；保留既有照片 |
| 创作草稿/示例 | 有 SD 用 `/sdcard/qpk/.draft` 和 `.examples` | 会话介质固定；SD 墓碑/损坏不读取旧内部草稿 |
| 草稿预览私有数据 | 跟随草稿身份 | 与正式安装应用隔离 |
| 笔记、别名、定时任务 | 有 SD 优先 SD | 定时任务不跨介质复制重放 |

关键回归按改动选择，运行前读各脚本参数：

- 配置：`tools/test_portal_storage.py`、`test_ai_config.py`、`test_ha_configuration.py`、`test_agent.py`，以及 `portal_frontend.test.cjs`（用 `rg --files` 定位当前路径）。
- 历史：`tools/test_chat_sd.py`、`test_camera_observation.py`。
- 照片：`tools/test_photo_storage.py`、`test_photo_store.py`、`test_assistant_platform.py`。
- 草稿/预览：`tools/test_draft_storage.py`、`test_install_volumes.py`。
- 配置退场：追加 `test_voice_requests.py`、`test_wake_handoff.py`、`test_speech_barge.py`。

生产入口在 `app/espdl-quickapp/overlay/apps/system/desktop/`。按数据路径搜索写入函数，再追踪包括失败、取消和普通历史快照的调用者，不能只修显眼的正常返回分支。

历史反例：2026-10-20 会话先把草稿保存改到 SD，用户运行小游戏仍闪；后续发现预览只传裸 `app.js`，运行时存储仍写 Flash，记录到 112 次写/10 次擦和 62.1 ms 显示间隔。这个反例要求检查整条存储身份传播链，不能把“草稿文件位于 SD”当成预览没有 Flash 写入的证据。
