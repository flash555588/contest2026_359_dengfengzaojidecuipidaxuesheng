# 语音入口与回归选择

路径相对项目根；先核对当前后端，不能从历史 SanoTTS 试验推断当前默认。模型或声音文件变更才需要读取 `app/espdl-quickapp/speech/esp-tts/WORKFLOW.md` 和 `speech/README.zh-CN.md`。

定位源码时在 `app/espdl-quickapp/overlay/apps/system/desktop/` 搜索 `glass_wake`、`glass_speech`、`request_id`、`RESERVE`、`CONFIGURE`、`RX`；底层音频实现从 overlay 的 NuttX 驱动入口追踪。

| 改动 | 优先回归 |
| --- | --- |
| 请求归属、切页接管 | `tools/test_voice_requests.py`、`tools/test_wake_handoff.py`、`tools/test_wake_mini.py` |
| 播报并行采集、停止 | `tools/test_speech_barge.py`、`tools/test_voice_wake_observer.py`、`tools/test_es8311_return.py` |
| 识别文字与完整服务链 | `tools/test_voice_transcript.py`、`tools/test_voice_pipeline.py` |
| 配置保存时停启服务 | `tools/test_ai_config.py` 及请求/打断/交接回归 |
| 检测器改变 | `tools/test_wake_scenarios.py`、`tools/test_wake_noise.py`、`tools/test_wake_babble.py` 等对应场景 |

先读所选脚本 argparse 和依赖；多数宿主 C 夹具在 Linux/WSL 运行。例如 `python3 tools/test_speech_barge.py --output /path/to/output/barge`。不要为 UI 图标修改重新训练模型，也不要为通过旧夹具修改已有产品重试语义。

历史证据：2026-10-19 对话指出全局浮球在其他页面正在录音时只显示 UI、未建立新录音请求；修复采用独立请求和旧结果隔离。2026-10-20/21 又补上实际播报 RX 观察者，回归和真人验收仍需分别报告。

2026-10-15 会话记录过 670 段合成语音和 MFCC/DTW 模板训练，明确测试集参与了开发，缺真人远场验收；后续改善回声的候选也降低了一组随机增广检出率。保留这类取舍，不能挑选单项数字宣传普遍可靠。
