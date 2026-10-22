# 项目入口与定向回归

只读取本次问题涉及的文件：

| 层次 | 项目根目录下的入口 |
| --- | --- |
| 运行契约 | `app/espdl-quickapp/HARNESS.md`、`agent.md`、`AGENT-CAPABILITIES.md` |
| 固件提示词源 | `app/espdl-quickapp/overlay/apps/system/desktop/agents/HARNESS.md` 及三个模式目录的 `agent.md` |
| 路由与协议 | `app/espdl-quickapp/overlay/apps/system/espclaw/port/` |
| 服务与界面回执 | `app/espdl-quickapp/overlay/apps/system/desktop/` |
| 助手工具说明 | `app/espdl-quickapp/ASSISTANT-TOOLS.md` |
| 相机观察 | `app/espdl-quickapp/MULTIMODAL-CAMERA.md` |

在 Linux/WSL、匹配的准备树中，从项目根运行。将占位路径换成当前实际路径，不能借用旧会话里的本机路径：

```sh
python3 tools/test_agent.py --apps /path/to/prepared/apps --output /path/to/output/agent
python3 tools/test_assistant_service.py --apps /path/to/prepared/apps --output /path/to/output/service
python3 tools/test_assistant_platform.py --baseline /path/to/prepared --output /path/to/output/platform
python3 tools/test_vision.py --apps /path/to/prepared/apps --output /path/to/output/vision
```

其他新工具按其服务选择测试；涉及观察采集再加 `test_camera_clip.py` 和 `test_camera_observation.py`，先检查各脚本参数。不要为普通提示词改动运行全部硬件测试。

当前文档记录的预算是 assistant 最多 16 轮/64 次调用、create 最多 8 轮/32 次调用；修改前核对当前实现。这些是项目参数，不是所有 Agent 的通用常数。

历史教训：相机采集成功、模型 HTTP 成功、正文解析成功、Agent 续答及实体屏幕正常，是不同检查点。2026-10-20 对话曾出现采集正常而模型返回 `CHAT_ERROR_RESPONSE`，也曾出现旧助手回执遮挡新结果；不能仅用其中一个检查点宣布完成。
