# ESPClaw Agent 工程入口

本文件供开发者阅读。真正进入模型的模式提示词在 `overlay/apps/system/desktop/agents/{chat,create,assistant}/agent.md`；通用运行契约在该目录的 `HARNESS.md`。仓库根的 `AGENTS.md` 约束开发工作。

## 能力与入口

| 模式 | 能力 | 代码边界 |
| --- | --- | --- |
| 聊天 | 流式文字／语音交流、独立历史 | 不暴露工具；拒绝猜测工具调用 |
| 创作 | QPK 示例、读取草稿、完整源码草稿、用户预览和保存 | 不能控制家居、调用 MCP 或自动安装 |
| 助手 | 应用／硬件、家居、音乐、笔记／待办／日程、定时任务、天气、播报、相机、多模态看图 | 参数校验、设备授权、回执与取消 |

`glass_chat_service.c` 管理请求队列、模式和轮次；`espclaw_chat.c` 管理模型核心生命周期与超时；`espclaw_qpk.c` 组装提示词、工具和上下文，执行调用预算；`glass_agent.c` 实现技能与 MCP、模式隔离及本地工具路由；`glass_board_assistant.c` 管理本地持久资料和调度；`glass_assistant_platform.c` 连接真实服务；`glass_vision.c` 负责单图／有界多帧分析，`qpk_camera_clip.c` 负责 Bot 的 USB JPEG 时序采集。

Bot 提供“端侧 · 人脸跟随”与“AI · 采集并上传”两个入口，默认不打开相机。后者由用户点击授权，约5秒采集保留3张JPEG，相机关闭后一次上传到已有视觉模型；聊天和助手可用，创作拒绝。模型无权触发这一入口。原图只在本次工作线程的有界内存中保留，结果文字按当前模式保存到对话。它是稀疏画面采样，无音频、无原始视频文件；不能当作完整视频理解。取消／失败后的普通重试不会重新录制上传，必须由用户再次点击。

完整执行流程、预算和故障恢复见 [Harness 框架](HARNESS.md)。已有设备、家居、音乐与技能说明见 [Agent 能力](AGENT-CAPABILITIES.md) 和 [助手工具](ASSISTANT-TOOLS.md)。默认 CSI 与 USB 的完整使用／部署见 [多模态摄像头](MULTIMODAL-CAMERA.md)。

## 提示词维护

为规避已复现的Flash保存／显示干扰，新的Bot相机观察及语音对话使用本次会话历史，成功结果独立写到`/sdcard/espclaw-observations/camera-<id>-<序号>.json`，包含问题、回答和实际采样时间戳，不保存图片。旧Flash历史保持原位。SD不可用时明确显示仅本次会话；重启后新观察从SD文件查看，不伪称仍在Flash历史中。打开ESPClaw保持Bot当前模式，传递正在进行的相机任务，允许查看完整结果。MiMo视觉子请求关闭默认思考、使用官方输出额度字段；主对话的模型和配置不变。

三种模式分别读取 `/sdcard/espclaw/agents/<mode>/agent.md`；文件缺失或无效则用固件默认。运行时 profile 当前不超过 8192 UTF-8 字节。SD 覆写不会替换固件强制附加的 Harness，也不能改变 C 层工具权限。不要将开发者 `AGENTS.md` 当成设备运行提示。

编辑 `.md` 后执行 `python3 tools/generate_agent_profiles.py`，生成 `glass_agent_profiles.h`；工具 schema 源为 `tools/generate_assistant_tools.py`，生成 `glass_assistant_tools.h`。`tools/test_agent.py` 会检查提示词与生成文件一致。

## 验收目标

1. 同一轮完整原话进入模型；否定、数值、单位与目标不能被预处理丢失。
2. 同轮发现／调用／回读使用真实设备身份；历史不证明当前状态。
3. 多步骤任务逐项报告，失败不掩盖已完成或未完成步骤。
4. 停止、断网、回执丢失、模型输出截断时不重放副作用。
5. 工具看图绑定已完成相机回执；Bot 多帧入口绑定本轮内存采集与显式点击。保存、端侧分类、云端分析分别验收。

本工程的 Harness 是现有 ESPClaw 核心上的运行约束和适配层，复用已有工具循环与会话管理；不另建第二套任务调度器。持续录像、通用机器人自主控制和多帧实时视频理解不由单张视觉工具提供。

接口与引脚依据官方开发板手册，见 [BOARD-INTERFACES.md](BOARD-INTERFACES.md)。

Bot、全局唤醒和语音面板发起的新对话使用相同会话策略：请求前、失败、取消及完成均不写Flash历史，成功文字独立保存SD；相机观察为camera-<id>-<序号>.json，语音对话为dialog-<id>-<序号>.json。长答案分页文件为sd-reply-<id>-<序号>.txt，只从实际挂载SD读取，不回退Flash。旧历史不迁移，文字工作台保留原有持久化策略。此措施仍须真实AI全流程和实体屏幕验收。
